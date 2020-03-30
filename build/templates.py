#!/usr/bin/env python3
#
# Build time helper that uses Jinja templating engine to inject
# Kconfig values into various template files.
#
# Kconfig values are exposed in two forms:
#
#   1) As a flat directly accessible variable, ie:
#
#       CONFIG_AAA=y
#       CONFIG_BBB=null
#       ...
#
#      Example on how to use flat variables in templates:
#
#      config_a={{ CONFIG_AAA }}
#
#   2) As a global variable KCONFIG_ALL that contain list of kconfig key/value
#      pairs that can be iterated, ie:
#
#       KCONFIG_ALL = [
#           { CONFIG_IFACE_AAA: y },
#           { CONFIG_IFACE_BBB: null },
#           ...
#       ]
#
#      Example on how to use key/value pairs in templates:
#
#      {% for key, value in KCONFIG_ALL.items() %}
#      {% if key.startswith('CONFIG_IFACE_') %}
#      {{ key }}={{ value }}
#      # or
#      {{ key }}={{ KCONFIG_ALL[key] }}
#      {% endfor %}
#
# Template files need use proper Jinja syntax. Docs available in:
#
#   * https://jinja.palletsprojects.com/en/2.10.x/
#
# Usage #1: process rootfs files
#
#   $ templates.py --process-rootfs <path-to-dir>
#
#   * Processes Jinja templates in given directory path using Kconfig values
#     available from environment. Files that are considered templates:
#     1) All files with ".jinja" suffix
#     2) All files that contain TEMPLATE_COMMENT
#   * Processed files are stripped of ".jinja" suffix and installed on same
#     path. After processing the template source file is removed in case of
#     *.jinja files.
#
# Usage #2: process ovsdb files
#
#   $ templates.py --process-ovsdb <path-to-file>
#
#   * Processes given Jinja template using Kconfig values available from
#     environment.
#   * Processed file is outputted to stdout.
#
# Dependencies:
#
#   $ pip3 install Jinja2
#

import os
import sys

import jinja2

KCONFIG_PREFIX      = 'CONFIG_'
TEMPLATE_SUFFIX     = '.jinja'
TEMPLATE_COMMENT    = '# {# jinja-parse #}\n'
OVSDB_SUFFIX        = '.json.jinja'

def get_kconfig_env():
    """Returns dict of Kconfig variable=value items."""
    env = {}
    for var in os.environ:
        if var.startswith(KCONFIG_PREFIX):
            env[var] = os.environ[var]
    # INSTALL_PREFIX is unquoted value of CONFIG_INSTALL_PREFIX
    env['INSTALL_PREFIX'] = os.environ['INSTALL_PREFIX']
    return env

def process_rootfs(dirname):
    """Processes .jinja files in given directory."""

    kconfig_vars = get_kconfig_env()

    for (dirpath, dirnames, filenames) in os.walk(dirname):
        for filename in filenames:

            src = os.path.join(dirpath, filename)
            dst = os.path.join(dirpath, filename.replace(TEMPLATE_SUFFIX, ''))
            env = jinja2.Environment(keep_trailing_newline=True)

            if os.path.islink(src):
                continue

            try:
                with open(src, 'r') as s:
                    content = s.read()
                    if filename.endswith(TEMPLATE_SUFFIX):
                        template = env.from_string(content)
                    elif TEMPLATE_COMMENT in content:
                        template = env.from_string(content.replace(TEMPLATE_COMMENT, ''))
                    else:
                        continue
            except UnicodeDecodeError:
                # Binary file
                continue
            except jinja2.TemplateSyntaxError as e:
                msg = "#### Jinja2 template syntax error: {}:{} error: {}\n".format(filename, e.lineno, e.message)
                sys.stderr.write('\x1b[31;1m' + msg.strip() + '\x1b[0;0m\n')
                raise e

            with open(dst, 'w') as d:
                d.write(template.render(KCONFIG_ALL=kconfig_vars, **kconfig_vars))

            if filename.endswith(TEMPLATE_SUFFIX):
                # Preserve original file permission
                os.chmod(dst, os.stat(src).st_mode)
                # Clenaup source template file
                os.unlink(src)

def process_ovsdb(filename):
    """Processes given ovsdb .json.jinja file"""

    kconfig_vars = get_kconfig_env()

    if not filename.endswith(OVSDB_SUFFIX):
        return

    with open(filename, 'r') as f:
        template = jinja2.Template(f.read())

    # Print to stdout
    print(template.render(KCONFIG_ALL=kconfig_vars, **kconfig_vars))

def main():

    if len(sys.argv) != 3:
        return

    if sys.argv[1] == '--process-rootfs':
        process_rootfs(sys.argv[2])

    if sys.argv[1] == '--process-ovsdb':
        process_ovsdb(sys.argv[2])

if __name__ == '__main__':
    main()
