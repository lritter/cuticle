#include "thumbnail.h"

static VipsAngle 
get_angle( VipsImage *im )
{
  VipsAngle angle;
  const char *orientation;

  angle = VIPS_ANGLE_0;

  if( vips_image_get_typeof( im, ORIENTATION ) && 
    !vips_image_get_string( im, ORIENTATION, &orientation ) ) {
    if( vips_isprefix( "6", orientation ) )
      angle = VIPS_ANGLE_90;
    else if( vips_isprefix( "8", orientation ) )
      angle = VIPS_ANGLE_270;
    else if( vips_isprefix( "3", orientation ) )
      angle = VIPS_ANGLE_180;

    /* Other values do rotate + mirror, don't bother handling them
     * though, how common can mirroring be. 
     *
     * See:
     *
     * http://www.80sidea.com/archives/2316
     */
  }

  return( angle );
}

/* Calculate the shrink factors. 
 *
 * We shrink in two stages: first, a shrink with a block average. This can
 * only accurately shrink by integer factors. We then do a second shrink with
 * a supplied interpolator to get the exact size we want.
 */
static int
calculate_shrink( VipsImage *im, double *residual, double* full_shrink, ThumbnailOptions options )
{
  int thumbnail_width = options.thumbnail_height;
  int thumbnail_height = options.thumbnail_height;
  gboolean rotate_image = options.rotate_image;
  gboolean crop_image = options.crop_image;

  VipsAngle angle = get_angle( im ); 
  gboolean rotate = angle == VIPS_ANGLE_90 || angle == VIPS_ANGLE_270;
  int width = rotate_image && rotate ? im->Ysize : im->Xsize;
  int height = rotate_image && rotate ? im->Xsize : im->Ysize;

  if( ((width <= thumbnail_width) && (height <= thumbnail_height)) && (options.resize_constraint == ONLY_SHRINK_LARGER)) {
    thumbnail_width = width;
    thumbnail_height = height;
  }

  vips_info(options.context_name, "O(%d,%d) T(%d,%d) R(%d)", width, height, thumbnail_width, thumbnail_height, options.resize_constraint);

  /* Calculate the horizontal and vertical shrink we'd need to fit the
   * image to the bounding box, and pick the biggest.
   *
   * In crop mode we aim to fill the bounding box, so we must use the
   * smaller axis.
   */
  double horizontal = (double) width / thumbnail_width;
  double vertical = (double) height / thumbnail_height;
  double factor = crop_image ?
    VIPS_MIN( horizontal, vertical ) : 
    VIPS_MAX( horizontal, vertical ); 

  vips_info(options.context_name, "Shrink Factor: %f", factor);

  /* If the shrink factor is <= 1.0, we need to zoom rather than shrink.
   * Just set the factor to 1 in this case.
   */
  double factor2 = factor < 1.0 ? 1.0 : factor;

  if(full_shrink) {
    *full_shrink = factor;
  }

  /* Int component of shrink.
   */
  int shrink = floor( factor2 );

  if( residual ) {
    /* Size after int shrink. We have to try with both axes since
     * if they are very different sizes we'll see different
     * rounding errors.
     */
    int iwidth = width / shrink;
    int iheight = height / shrink;

    /* Therefore residual scale factor is.
     */
    double hresidual = (width / factor) / iwidth; 
    double vresidual = (height / factor) / iheight; 

    *residual = VIPS_MAX( hresidual, vresidual ); 
  }

  return( shrink );
}

/* Find the best jpeg preload shrink.
 */
static int
thumbnail_find_jpegshrink( VipsImage *im, ThumbnailOptions options )
{
  int shrink = calculate_shrink( im, NULL, NULL, options );

  /* We can't use pre-shrunk images in linear mode. libjpeg shrinks in Y
   * (of YCbCR), not linear space.
   */

  if( options.linear_processing )
    return( 1 ); 
  else if( shrink >= 8 )
    return( 8 );
  else if( shrink >= 4 )
    return( 4 );
  else if( shrink >= 2 )
    return( 2 );
  else 
    return( 1 );
}

/* Open an image, returning the best version of that image for thumbnailing. 
 *
 * libjpeg supports fast shrink-on-read, so if we have a JPEG, we can ask 
 * VIPS to load a lower resolution version.
 */
static VipsImage *
thumbnail_open( VipsObject *process, const char *filename, ThumbnailOptions options )
{
  const char *loader;
  VipsImage *im;

  vips_info( options.context_name, "thumbnailing %s", filename );

  if( options.linear_processing )
    vips_info( options.context_name, "linear mode" ); 

  if( !(loader = vips_foreign_find_load( filename )) ) {
    return( NULL );
  }

  vips_info( options.context_name, "selected loader is %s", loader ); 

  if( strcmp( loader, "VipsForeignLoadJpegFile" ) == 0 ) {
    int jpegshrink;

    /* This will just read in the header and is quick.
     */
    if( !(im = vips_image_new_from_file( filename )) ) {
      return( NULL );
    }

    jpegshrink = thumbnail_find_jpegshrink( im, options );

    g_object_unref( im );

    vips_info( options.context_name, "loading jpeg with factor %d pre-shrink", jpegshrink ); 

    if( vips_foreign_load( filename, &im, "access", VIPS_ACCESS_SEQUENTIAL, "shrink", jpegshrink, NULL ) ) {
      return( NULL );
    }
  }
  else {
    /* All other formats.
     */
    if( vips_foreign_load( filename, &im, "access", VIPS_ACCESS_SEQUENTIAL, NULL ) ) {
      return( NULL );
    }
  }

  vips_object_local( process, im );

  return( im ); 
}

static VipsInterpolate *
thumbnail_interpolator( VipsObject *process, VipsImage *in, ThumbnailOptions options )
{
  double residual;
  VipsInterpolate *interp;

  calculate_shrink( in, &residual, NULL, options );

  /* For images smaller than the thumbnail, we upscale with nearest
   * neighbor. Otherwise we makes thumbnails that look fuzzy and awful.
   */
  if( !(interp = VIPS_INTERPOLATE( vips_object_new_from_string( 
    g_type_class_ref( VIPS_TYPE_INTERPOLATE ), 
    residual > 1.0 ? "nearest" : options.interpolator ) )) )
    return( NULL );

  vips_object_local( process, interp );

  return( interp );
}

/* Some interpolators look a little soft, so we have an optional sharpening
 * stage.
 */
static VipsImage *
thumbnail_sharpen( VipsObject *process, ThumbnailOptions options )
{
  VipsImage *mask;

  if( strcmp( options.convolution_mask, "none" ) == 0 ) 
    mask = NULL; 
  else if( strcmp( options.convolution_mask, "mild" ) == 0 ) {
    mask = vips_image_new_matrixv( 3, 3,
      -1.0, -1.0, -1.0,
      -1.0, 32.0, -1.0,
      -1.0, -1.0, -1.0 );
    vips_image_set_double( mask, "scale", 24 );
  }
  else
    if( !(mask = 
      vips_image_new_from_file( options.convolution_mask )) )
      vips_error_exit( "unable to load sharpen mask" ); 

  if( mask )
    vips_object_local( process, mask );

  return( mask );
}

static VipsImage *
thumbnail_shrink( VipsObject *process, VipsImage *in, VipsInterpolate *interp, VipsImage *sharpen, ThumbnailOptions options )
{
  VipsImage **t = (VipsImage **) vips_object_local_array( process, 10 );
  VipsInterpretation interpretation = options.linear_processing ? VIPS_INTERPRETATION_XYZ : VIPS_INTERPRETATION_sRGB; 

  int shrink; 
  double residual; 
  int tile_width;
  int tile_height;
  int nlines;

  /* RAD needs special unpacking.
   */
  if( in->Coding == VIPS_CODING_RAD ) {
    vips_info( options.context_name, "unpacking Rad to float" );

    /* rad is scrgb.
     */
    if( vips_rad2float( in, &t[0], NULL ) ) {
      return( NULL );
    }

    in = t[0];
  }

  /* In linear mode, we import right at the start. 
   *
   * This is only going to work for images in device space. If you have
   * an image in PCS which also has an attached profile, strange things
   * will happen. 
   */
  if( options.linear_processing &&
    in->Coding == VIPS_CODING_NONE &&
    (in->BandFmt == VIPS_FORMAT_UCHAR ||
     in->BandFmt == VIPS_FORMAT_USHORT) &&
    (vips_image_get_typeof( in, VIPS_META_ICC_NAME ) || 
     options.import_profile) ) {
    if( vips_image_get_typeof( in, VIPS_META_ICC_NAME ) ) {
      vips_info( options.context_name, "importing with embedded profile" );
    }
    else {
      vips_info( options.context_name, "importing with profile %s", options.import_profile ); 
    }

    if( vips_icc_import( in, &t[1], 
      "input_profile", options.import_profile,
      "embedded", TRUE,
      "pcs", VIPS_PCS_XYZ,
      NULL ) )  
      return( NULL );

    in = t[1];
  }

  /* To the processing colourspace. This will unpack LABQ as well.
   */
  vips_info( options.context_name, "converting to processing space %s",
             vips_enum_nick( VIPS_TYPE_INTERPRETATION, interpretation ) ); 

  if( vips_colourspace( in, &t[2], interpretation, NULL ) ) {
    return( NULL ); 
  }

  in = t[2];

  double full_shrink;
  shrink = calculate_shrink( in, &residual, &full_shrink, options );

  vips_info( options.context_name, "integer shrink by %d", shrink );

  if(full_shrink <= 1.0) {
    if( vips_shrink( in, &t[3], shrink, shrink, NULL ) ) {
      return( NULL );
    }

    in = t[3];
  }
  else {
    /* We're zooming the image instead so skip the "shrink" and go
     * straight for an affine trasformation. This is the expansion factor;
     */
    residual = 1.0 / full_shrink;
  }


  /* We want to make sure we read the image sequentially.
   * However, the convolution we may be doing later will force us 
   * into SMALLTILE or maybe FATSTRIP mode and that will break
   * sequentiality.
   *
   * So ... read into a cache where tiles are scanlines, and make sure
   * we keep enough scanlines to be able to serve a line of tiles.
   *
   * We use a threaded tilecache to avoid a deadlock: suppose thread1,
   * evaluating the top block of the output, is delayed, and thread2, 
   * evaluating the second block, gets here first (this can happen on 
   * a heavily-loaded system). 
   *
   * With an unthreaded tilecache (as we had before), thread2 will get
   * the cache lock and start evaling the second block of the shrink. 
   * When it reaches the png reader it will stall until the first block 
   * has been used ... but it never will, since thread1 will block on 
   * this cache lock. 
   */
  vips_get_tile_size( in, &tile_width, &tile_height, &nlines );

  if( vips_tilecache( in, &t[4], 
    "tile_width", in->Xsize,
    "tile_height", 10,
    "max_tiles", (nlines * 2) / 10,
    "access", VIPS_ACCESS_SEQUENTIAL,
    "threaded", TRUE, 
    NULL ) ||
    vips_affine( t[4], &t[5], residual, 0, 0, residual, 
      "interpolate", interp,
      NULL ) ) {
    return( NULL );
  }
  in = t[5];

  vips_info( options.context_name, "residual scale by %g", residual );
  vips_info( options.context_name, "%s interpolation", VIPS_OBJECT_GET_CLASS( interp )->nickname );

  /* Colour management.
   *
   * In linear mode, just export. In device space mode, do a combined
   * import/export to transform to the target space.
   */
  if( options.linear_processing ) {
    if( options.export_profile ||
      vips_image_get_typeof( in, VIPS_META_ICC_NAME ) ) {
      vips_info( options.context_name, "exporting to device space with a profile" );
      if( vips_icc_export( in, &t[7], "output_profile", options.export_profile, NULL ) ) {  
        return( NULL );
      }
      in = t[7];
    }
    else {
      vips_info( options.context_name, "converting to sRGB" );
      if( vips_colourspace( in, &t[6], VIPS_INTERPRETATION_sRGB, NULL ) ) {
        return( NULL ); 
      }
      in = t[6];
    }
  }
  else if( options.export_profile && (vips_image_get_typeof( in, VIPS_META_ICC_NAME ) ||  options.import_profile) ) {
    if( vips_image_get_typeof( in, VIPS_META_ICC_NAME ) ) {
      vips_info( options.context_name, "importing with embedded profile" );
    }
    else {
      vips_info( options.context_name, "importing with profile %s", options.import_profile );
    }

    vips_info( options.context_name, "exporting with profile %s", options.export_profile );

    if( vips_icc_transform( in, &t[6], options.export_profile, "input_profile", options.import_profile, "embedded", TRUE, NULL ) ) {
      return( NULL );
    }

    in = t[6];
  }

  /* If we are upsampling, don't sharpen, since nearest looks dumb
   * sharpened.
   */
  if( shrink >= 1 && 
    residual <= 1.0 && 
    sharpen ) { 
    vips_info( options.context_name, "sharpening thumbnail" );
    if( vips_conv( in, &t[8], sharpen, NULL ) ) {
      return( NULL );
    }
    in = t[8];
  }

  if( options.delete_profile &&
    vips_image_get_typeof( in, VIPS_META_ICC_NAME ) ) {
    vips_info( options.context_name, "deleting profile from output image" );
    if( !vips_image_remove( in, VIPS_META_ICC_NAME ) ) 
      return( NULL );
  }

  return( in );
}

/* Crop down to the final size, if crop_image is set. 
 */
static VipsImage *
thumbnail_crop( VipsObject *process, VipsImage *im, ThumbnailOptions options )
{
  VipsImage **t = (VipsImage **) vips_object_local_array( process, 2 );

  if( options.crop_image ) {
    int left = (im->Xsize - options.thumbnail_width) / 2;
    int top = (im->Ysize - options.thumbnail_height) / 2;

    if( vips_extract_area( im, &t[0], left, top, options.thumbnail_width, options.thumbnail_height, NULL ) ) {
      return( NULL ); 
    }
    im = t[0];
  }

  return( im );
}

/* Auto-rotate, if rotate_image is set. 
 */
static VipsImage *
thumbnail_rotate( VipsObject *process, VipsImage *im, ThumbnailOptions options )
{
  VipsImage **t = (VipsImage **) vips_object_local_array( process, 1 );

  if( options.rotate_image ) {
    VipsAngle angle = get_angle( im );
    if( vips_rot( im, &t[0], angle, NULL ) ) {
      vips_info(options.context_name, "failed to rotation image %d", angle);

      return( NULL );
    }
       
    im = t[0];

    vips_info(options.context_name, "rotated image");
    (void) vips_image_remove( im, ORIENTATION );
  }

  return( im );
}

/* Given (eg.) "/poop/somefile.png", write @im to the thumbnail name,
 * (eg.) "/poop/tn_somefile.jpg".
 */
static int
thumbnail_write( VipsImage *im, const char *filename, ThumbnailOptions options )
{
  char *file;
  char *p;
  char buf[FILENAME_MAX];
  char *output_name;

  file = g_path_get_basename( filename );

  /* Remove the suffix from the file portion.
   */
  if( (p = strrchr( file, '.' )) ) 
    *p = '\0';

  /* output_format can be an absolute path, in which case we discard the
   * path from the incoming file.
   */
  vips_snprintf( buf, FILENAME_MAX, options.output_format, file );
  /* Stock vipsthumbnail does some stupid stuf with relative file names. 
   * Ignore that, just use the path we're given.
   */
  output_name = g_strdup( buf );

  vips_info( options.context_name, "thumbnailing %s as %s", filename, output_name );

  g_free( file );

  if( vips_image_write_to_file( im, output_name ) ) {
    g_free( output_name );
    return( -1 );
  }
  g_free( output_name );

  return( 0 );
}

int
thumbnail_process( VipsObject *process, const char *filename, ThumbnailOptions options )
{
  VipsImage *sharpen = thumbnail_sharpen( process, options );

  VipsImage *in;
  VipsInterpolate *interp;
  VipsImage *thumbnail;
  VipsImage *crop;
  VipsImage *rotate;

  if( !(in = thumbnail_open( process, filename, options )) ||
    !(interp = thumbnail_interpolator( process, in, options )) ||
    !(thumbnail = 
      thumbnail_shrink( process, in, interp, sharpen, options )) ||
    !(crop = thumbnail_crop( process, thumbnail, options )) ||
    !(rotate = thumbnail_rotate( process, crop, options )) ||
    thumbnail_write( rotate, filename, options ) )
    return( -1 );

  return( 0 );
}

int
simple_transform(const char* filename, ThumbnailOptions options) {
  int error = 0;

  if( vips_init( filename ) ) {
    vips_error( options.context_name, "unable to start VIPS" );
    error = 1;
  }
  else {
    VipsObject *process = VIPS_OBJECT( vips_image_new() ); 

    if( thumbnail_process( process, filename, options ) ) {
      error = 2;
      fprintf( stderr, "%s: unable to thumbnail %s\n", options.context_name, filename );
      fprintf( stderr, "%s", vips_error_buffer() );
      vips_error_clear();
    }

    g_object_unref( process );
    vips_shutdown();
  }
  
  return error;
}