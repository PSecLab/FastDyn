#!/usr/bin/env python

import os
from distutils.core import setup


def get_packages(rel_dir):
    packages = [rel_dir]
    for x in os.walk(rel_dir):
        # break into parts
        base = list(os.path.split(x[0]))
        if base[0] == "":
            del base[0]

        for mod_name in x[1]:
            packages.append(".".join(base + [mod_name]))

    return packages


setup(
    name='fastdyn',
    version='0.1.0',
    description='FastDyn: Config parser and logging framework',
    author='Fastdyn Team',
    packages=get_packages('fastdyn'),
    entry_points={
        'console_scripts': [
            'fastdyn = fastdyn.main:cli',
            # you can add more CLI tools here
        ]
    },
    requires=[
        'PyYAML',
        'tomli',
    ]
)
