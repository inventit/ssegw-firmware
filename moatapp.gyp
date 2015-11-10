{ 'includes': [
    'common.gypi',
    'config.gypi',
  ],
  'targets': [
    # your M2M/IoT application
    {
      'target_name': '<(package_name)',
      'sources': [
        'src/<(package_name).c',
        'src/firmware/download_info_model.c',
        'src/firmware/firmware_updater.c',
       ],
      'product_prefix': '',
      'type': 'shared_library',
      'cflags': [ '-fPIC' ],
      'include_dirs' : [
      ],
      'libraries': [
      ],
      'dependencies': [
      ],
    },

  ],
}
