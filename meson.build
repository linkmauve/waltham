project('waltham', 'c',
        version: '0.0.0',
        license: ['MIT'],
        meson_version: '>=0.41.0')
version = meson.project_version()
desc = 'Network IPC library designed to resemble Wayland'

pkgconfig = import('pkgconfig')
python2 = find_program('python2')
gen_py = find_program('tools/gen.py')

libwaltham_src = [
    'src/waltham/demarshaller.h',
    'src/waltham/marshaller.c',
    'src/waltham/marshaller.h',
    'src/waltham/marshaller_log.h',
    'src/waltham/message.c',
    'src/waltham/message.h',
    'src/waltham/waltham-connection.c',
    'src/waltham/waltham-connection.h',
    'src/waltham/waltham-object.c',
    'src/waltham/waltham-object.h',
    'src/waltham/waltham-private.h',
    'src/waltham/waltham-util.c',
    'src/waltham/waltham-util.h',
]

libwaltham_headers = [
    'src/waltham/waltham-connection.h',
    'src/waltham/waltham-object.h',
    'src/waltham/waltham-util.h',
]

protocols_src = [
    'data/private.xml',
    'data/public.xml',
    'data/command.xml',
]

client_h = custom_target('waltham-client.h',
                         input: ['src/waltham/header-preamble.txt', protocols_src],
                         output: 'waltham-client.h',
                         depend_files: 'tools/gen.py',
                         command: [gen_py, '-p', '@INPUT0@', '-i', '@INPUT1@', '-i', '@INPUT2@', '-i', '@INPUT3@', '-o', '@OUTPUT@', '-m', 'client', '-t', 'header'],
                         install: true,
                         install_dir: 'include/waltham')

client_serialice = custom_target('client-serialice.c',
                                 input: ['src/waltham/serial-preamble.txt', protocols_src],
                                 output: 'client-serialice.c',
                                 depend_files: 'tools/gen.py',
                                 depends: client_h,
                                 command: [gen_py, '-p', '@INPUT0@', '-i', '@INPUT1@', '-i', '@INPUT2@', '-i', '@INPUT3@', '-o', '@OUTPUT@', '-m', 'client', '-t', 'marshaller'])

client_deserialice = custom_target('client-deserialice.c',
                                   input: ['src/waltham/deserial-preamble.txt', protocols_src],
                                   output: 'client-deserialice.c',
                                   depend_files: 'tools/gen.py',
                                   depends: client_h,
                                   command: [gen_py, '-p', '@INPUT0@', '-i', '@INPUT1@', '-i', '@INPUT2@', '-i', '@INPUT3@', '-o', '@OUTPUT@', '-m', 'client', '-t', 'demarshaller'])

server_h = custom_target('waltham-server.h',
                         input: ['src/waltham/header-preamble.txt', protocols_src],
                         output: 'waltham-server.h',
                         depend_files: 'tools/gen.py',
                         command: [gen_py, '-p', '@INPUT0@', '-i', '@INPUT1@', '-i', '@INPUT2@', '-i', '@INPUT3@', '-o', '@OUTPUT@', '-m', 'server', '-t', 'header'],
                         install: true,
                         install_dir: 'include/waltham')

server_serialice = custom_target('server-serialice.c',
                                 input: ['src/waltham/serial-preamble.txt', protocols_src],
                                 output: 'server-serialice.c',
                                 depend_files: 'tools/gen.py',
                                 depends: server_h,
                                 command: [gen_py, '-p', '@INPUT0@', '-i', '@INPUT1@', '-i', '@INPUT2@', '-i', '@INPUT3@', '-o', '@OUTPUT@', '-m', 'server', '-t', 'marshaller'])

server_deserialice = custom_target('server-deserialice.c',
                                   input: ['src/waltham/deserial-preamble.txt', protocols_src],
                                   output: 'server-deserialice.c',
                                   depend_files: 'tools/gen.py',
                                   depends: server_h,
                                   command: [gen_py, '-p', '@INPUT0@', '-i', '@INPUT1@', '-i', '@INPUT2@', '-i', '@INPUT3@', '-o', '@OUTPUT@', '-m', 'server', '-t', 'demarshaller'])

generated_src = [
    client_h,
    client_serialice,
    client_deserialice,
    server_h,
    server_serialice,
    server_deserialice,
]

add_global_arguments([
    '-Wall',
    '-Wextra',
    '-Wno-format-zero-length',
    '-Wno-unused-parameter',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wno-missing-field-initializers',
    '-fvisibility=hidden',
], language: 'c')

libwaltham_inc = include_directories('src/waltham')
libwaltham = shared_library('waltham', libwaltham_src, generated_src,
                            include_directories: libwaltham_inc,
                            link_args: '-Wl,--no-undefined',
                            install: true,
                            version: version,
                            soversion: version[0])
install_headers(libwaltham_headers, subdir: 'waltham')
pkgconfig.generate(libraries: libwaltham,
                   subdirs: 'waltham',
                   version: version,
                   name: 'Waltham',
                   filebase: 'waltham',
                   description: desc)

tests_src = [
    'tests/w-util.c',
    'tests/w-util.h',
]

client_src = ['tests/client-api-example.c', client_h, tests_src]
client_exe = executable('client', client_src, include_directories: libwaltham_inc, link_with: libwaltham)
test('client', client_exe)

server_src = ['tests/server-api-example.c', server_h, tests_src]
server_exe = executable('server', server_src, include_directories: libwaltham_inc, link_with: libwaltham)
test('server', server_exe)

if get_option('documentation')
    doxygen = find_program('doxygen')
    sed = find_program('sed')

    doxygen_version_cmd = run_command(doxygen.path(), '--version')
    if doxygen_version_cmd.returncode() != 0
        error('Command "doxygen --version" failed.')
    endif
    doxygen_version = doxygen_version_cmd.stdout()
    if doxygen_version.version_compare('< 1.8.0')
        error('doxygen needs to be at least version 1.8.0 (have @0@)'.format(doxygen_version))
    endif

    mainpage_md = custom_target('mainpage.md',
                                input: 'README.md',
                                output: 'mainpage.md',
                                command: [sed, '-e', 's|^Waltham IPC Library$|Waltham IPC Library {#mainpage}|', '@INPUT@'],
                                capture: true)

    doc_src = [
        mainpage_md,
        'doc/api_groups.dox',
        'doc/usage.dox',
        'src/waltham/waltham-connection.h',
        'src/waltham/waltham-object.h',
        'src/waltham/waltham-util.h',
        client_h,
        server_h,
    ]

    doc_config = configuration_data()
    doc_config.set('PACKAGE_NAME', meson.project_name())
    doc_config.set('VERSION', version)
    doc_config.set('top_srcdir', meson.source_root())
    doc_config.set('top_builddir', meson.build_root())

    doxyfile = configure_file(input: 'doc/doxygen.conf.in',
                              output: 'doxygen.conf',
                              configuration: doc_config)

    custom_target('doxygen',
                  input: [doxyfile, doc_src],
                  output: 'html',
                  command: [doxygen, doxyfile],
                  build_by_default: true)
endif
