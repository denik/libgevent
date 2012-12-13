#!/usr/bin/env python
import sys
import os
import traceback
from os.path import abspath, basename
from pipes import quote

try:
    from setuptools import Extension, setup
except ImportError:
    from distutils.core import Extension, setup
from distutils.command import build_ext


class my_build_ext(build_ext.build_ext):

    def build_extension(self, ext):
        if os.system('cd ../libuv && make'):
            sys.exit('make failed')
        if os.system('make libgevent.c'):
            sys.exit('make failed')
        result = build_ext.build_ext.build_extension(self, ext)
        try:
            fullname = self.get_ext_fullname(ext.name)
            modpath = fullname.split('.')
            filename = self.get_ext_filename(ext.name)
            filename = os.path.split(filename)[-1]
            if not self.inplace:
                filename = os.path.join(*modpath[:-1] + [filename])
                path_to_build_core_so = abspath(os.path.join(self.build_lib, filename))
                path_to_core_so = abspath(basename(path_to_build_core_so))
                if path_to_build_core_so != path_to_core_so:
                    cmd = 'cp %s %s' % (quote(path_to_build_core_so), quote(path_to_core_so))
                    #print cmd
                    os.system(cmd)
        except Exception:
            traceback.print_exc()
        return result


EXT = Extension(name='libgevent',
                sources=['libgevent.c'],
                include_dirs=['../include', '..', '../libuv/include'],
                libraries=['rt'],
                extra_objects=['../libuv/libuv.a']
               )


if __name__ == '__main__':
    setup(
        name='gevent',
        version='2.0dev',
        author='Denis Bilenko',
        author_email='denis.bilenko@gmail.com',
        ext_modules=[EXT],
        cmdclass={'build_ext': my_build_ext},
        classifiers=[
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Intended Audience :: Developers",
        "Development Status :: 4 - Beta"])
