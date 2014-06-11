/* VIPS thumbnailer
 *
 * 11/1/09
 *
 * 13/1/09
 *  - decode labq and rad images
 *  - colour management
 *  - better handling of tiny images
 * 25/1/10
 *  - added "--delete"
 * 6/2/10
 *  - added "--interpolator"
 *  - added "--nosharpen"
 *  - better 'open' logic, test lazy flag now
 * 13/5/10
 *  - oops hehe residual sharpen test was reversed
 *  - and the mask coefficients were messed up
 * 26/5/10
 *  - delete failed if there was a profile
 * 4/7/10
 *  - oops sharpening was turning off for integer shrinks, thanks Nicolas
 * 30/7/10
 *  - use new "rd" mode rather than our own open via disc
 * 8/2/12
 *  - use :seq mode for png images
 *  - shrink to a scanline cache to ensure we request pixels sequentially
 *    from the input
 * 13/6/12
 *  - update the sequential stuff to the general method
 * 21/6/12
 *  - remove "--nodelete" option, have a --delete option instead, off by
 *    default
 *  - much more gentle extra sharpening
 * 13/11/12
 *  - allow absolute paths in -o (thanks fuho)
 * 3/5/13
 *  - add optional sharpening mask from file
 * 10/7/13
 *  - rewrite for vips8
 *  - handle embedded jpeg thumbnails
 * 12/11/13
 *  - add --linear option
 * 18/12/13
 *  - add --crop option
 * 5/3/14
 *  - copy main image metadata to embedded thumbnails, thanks ottob
 * 6/3/14
 *  - add --rotate flag
 * 7/3/14
 *  - remove the embedded thumbnail reader, embedded thumbnails are too
 *    unlike the main image wrt. rotation / colour / etc.
 */


#include "thumbnail.h"
#include <locale.h>
#include <regex.h>

#define GETTEXT_PACKAGE ("")

static char* default_cuticle_context_name = "cuticle";
static char* context_name_arg = NULL;
static char *thumbnail_size = "128";
static int thumbnail_width = 128;
static int thumbnail_height = 128;
static char *output_format = "tn_%s.jpg";
static char *interpolator = "bilinear";
static char *export_profile = NULL;
static char *import_profile = NULL;
static char *convolution_mask = "mild";
static gboolean delete_profile = FALSE;
static gboolean linear_processing = FALSE;
static gboolean crop_image = FALSE;
static gboolean rotate_image = FALSE;

/* Deprecated and unused.
 */
static gboolean nosharpen = FALSE;
static gboolean nodelete_profile = FALSE;
static gboolean verbose = FALSE;

static ResizeConstraint resize_constraint = ONLY_SHRINK_LARGER;

static GOptionEntry options[] = {
  { "size", 's', 0, 
    G_OPTION_ARG_STRING, &thumbnail_size, 
    N_( "shrink to SIZE or to WIDTHxHEIGHT" ), 
    N_( "SIZE" ) },
  { "output", 'o', 0, 
    G_OPTION_ARG_STRING, &output_format, 
    N_( "set output to FORMAT" ), 
    N_( "FORMAT" ) },
  { "interpolator", 'p', 0, 
    G_OPTION_ARG_STRING, &interpolator, 
    N_( "resample with INTERPOLATOR" ), 
    N_( "INTERPOLATOR" ) },
  { "sharpen", 'r', 0, 
    G_OPTION_ARG_STRING, &convolution_mask, 
    N_( "sharpen with none|mild|MASKFILE" ), 
    N_( "none|mild|MASKFILE" ) },
  { "eprofile", 'e', 0, 
    G_OPTION_ARG_STRING, &export_profile, 
    N_( "export with PROFILE" ), 
    N_( "PROFILE" ) },
  { "iprofile", 'i', 0, 
    G_OPTION_ARG_STRING, &import_profile, 
    N_( "import untagged images with PROFILE" ), 
    N_( "PROFILE" ) },
  { "context", 'x', 0, 
    G_OPTION_ARG_STRING, &context_name_arg, 
    N_( "tag log items with CONTEXT" ), 
    N_( "CONTEXT" ) },
  { "linear", 'a', 0, 
    G_OPTION_ARG_NONE, &linear_processing, 
    N_( "process in linear space" ), NULL },
  { "crop", 'c', 0, 
    G_OPTION_ARG_NONE, &crop_image, 
    N_( "crop exactly to SIZE" ), NULL },
  { "rotate", 't', 0, 
    G_OPTION_ARG_NONE, &rotate_image, 
    N_( "auto-rotate" ), NULL },
  { "delete", 'd', 0, 
    G_OPTION_ARG_NONE, &delete_profile, 
    N_( "delete profile from exported image" ), NULL },
  { "verbose", 'v', G_OPTION_FLAG_HIDDEN, 
    G_OPTION_ARG_NONE, &verbose, 
    N_( "(deprecated, does nothing)" ), NULL },
  { "nodelete", 'l', G_OPTION_FLAG_HIDDEN, 
    G_OPTION_ARG_NONE, &nodelete_profile, 
    N_( "(deprecated, does nothing)" ), NULL },
  { "nosharpen", 'n', G_OPTION_FLAG_HIDDEN, 
    G_OPTION_ARG_NONE, &nosharpen, 
    N_( "(deprecated, does nothing)" ), NULL },
  { NULL }
};


int
parse_thumbnail_size(char *thumbnail_size, int* w, int* h, ResizeConstraint* constraint) {
  regex_t regex;
  int status = 0;
  regmatch_t pmatch[5];

  status = regcomp(&regex, "^([[:digit:]]+)(x([[:digit:]]+))?(.)?$", REG_EXTENDED);

  if( status ){ 
    fprintf(stderr, "Could not compile regex: %d", status);
    return status;
  }

  status = regexec(&regex, thumbnail_size, 5, pmatch, 0);
  regfree(&regex);

  if(!status) {
    if(pmatch[1].rm_so != -1) {
      *w = atoi(&thumbnail_size[pmatch[1].rm_so]);

      if(pmatch[3].rm_so != -1) {
        *h = atoi(&thumbnail_size[pmatch[3].rm_so]);
      }
      else {
        *h = *w;
      }

      if(pmatch[4].rm_so != -1) {
        if(thumbnail_size[pmatch[4].rm_so] == '^') {
          *constraint = FILL_AREA;
        }
        else {
          *constraint = ONLY_SHRINK_LARGER;
        }

      }
    }
  }
  else {
    printf("No Match for: '%s'", thumbnail_size);
    return 1;
  }

  return status;
}

int
main( int argc, char **argv )
{
  GOptionContext *context;
  GError *error = NULL;
  int i;

  if( vips_init( argv[0] ) ){
    vips_error_exit( "unable to start VIPS" );
  }
          
  textdomain( GETTEXT_PACKAGE );
  setlocale( LC_ALL, "" );

  context = g_option_context_new( _( "- thumbnail generator" ) );

  g_option_context_add_main_entries( context, options, GETTEXT_PACKAGE );
  g_option_context_add_group( context, vips_get_option_group() );

  if( !g_option_context_parse( context, &argc, &argv, &error ) ) {
    if( error ) {
      fprintf( stderr, "%s\n", error->message );
      g_error_free( error );
    }

    vips_error_exit( "try \"%s --help\"", g_get_prgname() );
  }

  g_option_context_free( context );

  if(parse_thumbnail_size(thumbnail_size, &thumbnail_width, &thumbnail_height, &resize_constraint)) {
    fprintf( stderr, "Undable to parse thumbnail size: '%s'\n", thumbnail_size);
    exit(1);
  }

  for( i = 1; i < argc; i++ ) {
    /* Hang resources for processing this thumbnail off @process.
     */
    VipsObject *process = VIPS_OBJECT( vips_image_new() ); 

    ThumbnailOptions thumb_options = ThumbnailOptionsWithDefaults();
    thumb_options.thumbnail_height = thumbnail_height;
    thumb_options.thumbnail_width = thumbnail_width;
    thumb_options.crop_image = crop_image;
    thumb_options.rotate_image = rotate_image;
    thumb_options.convolution_mask = convolution_mask;
    thumb_options.interpolator = interpolator;
    thumb_options.import_profile = import_profile;
    thumb_options.export_profile = export_profile;
    thumb_options.delete_profile = delete_profile;
    thumb_options.output_format = output_format;
    thumb_options.resize_constraint = resize_constraint;

    if(context_name_arg) {
      char* buffer = malloc(sizeof(char)*(strlen(context_name_arg) + strlen(default_cuticle_context_name) + 2)); // buffer is long enough for ' ' and null terminator
      sprintf(buffer, "%s %s", default_cuticle_context_name, context_name_arg);
      thumb_options.context_name = buffer;
    }

    if( thumbnail_process( process, argv[i], thumb_options ) ) {
      fprintf( stderr, "%s: unable to thumbnail %s\n", 
        argv[0], argv[i] );
      fprintf( stderr, "%s", vips_error_buffer() );
      vips_error_clear();
      exit(1);
    }

    g_object_unref( process );
  }

  vips_shutdown();

  return( 0 );
}
