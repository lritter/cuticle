{
  "targets": [
    {
      "target_name": "cuticle_lib",
      "target_type": "library_static",
      "sources": [ 
        "src/thumbnail.c",
        "src/vipsthumbnail.c"
      ],

      "conditions": [
        ['OS=="mac"', {
          'libraries': [
              '<!@(PKG_CONFIG_PATH=/usr/local/Library/ENV/pkgconfig/10.8 pkg-config --libs glib-2.0 vips)',
          ],
          'include_dirs': [
            '/usr/local/include/glib-2.0',
            '/usr/local/include/vips',
            '/usr/local/lib/glib-2.0/include',
            './src'
          ]
        }, {
          'libraries': [
              '<!@(PKG_CONFIG_PATH="/usr/local/lib/pkgconfig" pkg-config --libs glib-2.0 vips)'
          ],
          'include_dirs': [
              '/usr/include/glib-2.0',
              '/usr/lib/glib-2.0/include',
              '/usr/lib/x86_64-linux-gnu/glib-2.0/include',
              'src'
          ],
        }]
      ]
    },

    {
      "target_name": "hangnail",
      "type": 'executable',
      "sources": [
        "src/vipsthumbnail.c",
        "src/thumbnail.c"
      ],

      "dependencies": [ 'cuticle_lib' ],

      "conditions": [
        ['OS=="mac"', {
          'libraries': [
              '<!@(PKG_CONFIG_PATH=/usr/local/Library/ENV/pkgconfig/10.8 pkg-config --libs glib-2.0 vips)',
          ],
          'include_dirs': [
            '/usr/local/include/glib-2.0',
            '/usr/local/include/vips',
            '/usr/local/lib/glib-2.0/include',
            './src'
          ]
        }]
      ]
    },

    {
      "target_name": "cuticle",
      "sources": [ 
        "src/thumbnail.c",
        "src/cuticle.cpp" 
      ],

      "dependencies": [ 'cuticle_lib' ],

      "conditions": [
        ['OS=="mac"', {
          'libraries': [
              '<!@(PKG_CONFIG_PATH=/usr/local/Library/ENV/pkgconfig/10.8 pkg-config --libs glib-2.0 vipsCC)',
          ],
          'include_dirs': [
            '/usr/local/include/glib-2.0',
            '/usr/local/include/vips',
            '/usr/local/lib/glib-2.0/include'
          ]
        }, {
          'libraries': [
              '<!@(PKG_CONFIG_PATH="/usr/local/lib/pkgconfig" pkg-config --libs glib-2.0 vipsCC)'
          ],
          'include_dirs': [
              '/usr/include/glib-2.0',
              '/usr/lib/glib-2.0/include',
              '/usr/lib/x86_64-linux-gnu/glib-2.0/include'
          ],
        }]
      ]
    },
  ]
}