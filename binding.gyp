{
  "targets": [
    {
      "target_name": "python-ts",
      "sources": [ "src/binding.cc" ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<!@(node -p \"require('./src/gyp').include_dirs\")"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "link_settings": {
        "libraries": [
          "<!@(node -p \"require('./src/gyp').libraries\")"
        ],
        "library_dirs": [
          "<!@(node -p \"require('./src/gyp').library_dirs\")"
        ]
      },
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"]
    }
  ],
}