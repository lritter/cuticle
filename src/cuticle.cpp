#include <node.h>
#include <iostream>

extern "C" {
  #include "thumbnail.h"
}

static const std::string CROP_STYLE_ASPECTFIT = "aspectfit";
static const std::string CROP_STYLE_ASPECTFILL = "aspectfill";

using namespace v8;

// Forward declaration:
static int Transform(std::string& sourcePath, int width, int height, std::string& aspect, std::string& outputPath) {
  ThumbnailOptions options = ThumbnailOptionsWithDefaults();
  options.thumbnail_width = width;
  options.thumbnail_height = height;
  options.crop_image = CROP_STYLE_ASPECTFILL.compare(aspect) == 0;
  options.output_format = outputPath.c_str();

  return simple_transform(sourcePath.c_str(), options);
}

Handle<Value> NodeTransformImage(const Arguments& args) {
  HandleScope scope;

  // Check that there are enough arguments. If we access an index that doesn't
  // exist, it'll be Undefined().
  if(args.Length() != 6) {
    // Throw an exception to alert the user to incorrect usage.
    return scope.Close(ThrowException(
      Exception::TypeError(String::New("Must pass 7 arguments: "
        "input path (String), "
        "width (Integer), "
        "height (Integer), "
        "aspect handling (String), "
        "output path (String), "
        "callback (Function)"
      ))
    ));
  }

  v8::String::Utf8Value srcPathUtf8Value(args[0]->ToString());
  std::string srcPath = std::string(*srcPathUtf8Value);

  v8::String::Utf8Value aspectUtf8Value(args[3]->ToString());
  std::string aspectHandling = std::string(*aspectUtf8Value);

  v8::String::Utf8Value destPathUtf8Value(args[4]->ToString());
  std::string destPath = std::string(*destPathUtf8Value);

  Local<Function> callback = Local<Function>::Cast(args[5]);

  const unsigned argc = 2;
  Local<Value> argv[argc] = {
    Local<Value>::New(Null()),
    args[4]
  };

  int error = Transform(srcPath, 
                args[1]->ToInteger()->Value(), 
                args[2]->ToInteger()->Value(), 
                aspectHandling, 
                destPath
               );

  if(error) {
    argv[0] = Integer::New(error);
  }

  callback->Call(Context::GetCurrent()->Global(), argc, argv);

  return scope.Close(Undefined());
}

void RegisterModule(v8::Handle<v8::Object> target) {
  // You can add properties to the module in this function. It is called
  // when the module is required by node.
  target->Set(String::NewSymbol("transform"),
              FunctionTemplate::New(NodeTransformImage)->GetFunction());
}

// Register the module with node. Note that "modulename" must be the same as
// the basename of the resulting .node file. You can specify that name in
// binding.gyp ("target_name"). When you change it there, change it here too.
NODE_MODULE(cuticle, RegisterModule);