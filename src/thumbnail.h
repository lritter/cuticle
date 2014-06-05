#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /*HAVE_CONFIG_H*/
#include <vips/intl.h>

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <vips/vips.h>

#define ORIENTATION ("exif-ifd0-Orientation")

typedef enum {
  ONLY_SHRINK_LARGER, // '>''
  FILL_AREA
} ResizeConstraint;

typedef struct {
  int thumbnail_width;
  int thumbnail_height;
  gboolean rotate_image;
  gboolean crop_image;
  ResizeConstraint resize_constraint;
  
  gboolean linear_processing;
  const char* convolution_mask;
  const char* interpolator;

  const char* export_profile;
  const char* import_profile;
  gboolean delete_profile;

  const char* output_format;
  const char* context_name;
} ThumbnailOptions;

inline
ThumbnailOptions ThumbnailOptionsWithDefaults() {
    ThumbnailOptions options = {
    -1,           // thumbnail_width
    -1,           // thumbnail_height
    TRUE,         // rotate
    FALSE,        // crop
    FILL_AREA, // Don't shrink if too small

    FALSE,        // linear_processing
    "mild",       // convolution_mask
    "bilinear",   // interpolator

    NULL,         // export_profile
    NULL,         // import_profile
    FALSE,        // delete_profile

    NULL,          // output_format
    "cuticle"
  };

  return options;
}

int
thumbnail_process( VipsObject *process, const char *filename, ThumbnailOptions options );

int
simple_transform(const char* filename, ThumbnailOptions options);

