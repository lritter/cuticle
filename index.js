var cuticle = require('./build/Release/cuticle');

console.warn(cuticle);
cuticle.transform("./left.jpg", 64, 64, "aspectfill", "./64x64.jpg[strip]", function(err, res) {
  if(err) {
    console.dir(err);
  }
  else {
    console.dir(res);
  }
});