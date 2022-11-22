import io
from setuptools import setup

with io.open("README.md", encoding="utf-8") as readme:
    long_description = readme.read()

setup(name="psautohint",
      description="Python wrapper for Adobe's PostScript autohinter",
      long_description=long_description,
      long_description_content_type='text/markdown',
      url='https://github.com/adobe-type-tools/psautohint',
      author='Adobe Type team & friends',
      author_email='afdko@adobe.com',
      license='Apache License, Version 2.0',
      package_dir={'': 'python'},
      packages=['psautohint'],
      entry_points={
          'console_scripts': [
              "psautohint = psautohint.__main__:main",
              "psstemhist = psautohint.__main__:stemhist",
              "splitpsdicts = psautohint.splitpsdicts:main",
          ],
      },
      python_requires='>3.7',
      install_requires=[
          'fonttools[ufo]>=4.22.0',
      ],
      extras_require={
          "testing": [
              "pytest >= 3.0.0, <4",
              "pytest-cov >= 2.5.1, <3",
              "pytest-xdist >= 1.22.2, <1.28.0",
              "pytest-randomly >= 1.2.3, <2",
          ],
      },
      zip_safe=False,
      classifiers=[
          'Development Status :: 5 - Production/Stable',
          'Environment :: Console',
          'Intended Audience :: Developers',
          'License :: OSI Approved :: Apache Software License',
          'Natural Language :: English',
          'Operating System :: OS Independent',
          'Programming Language :: Python',
          'Programming Language :: Python :: 3 :: Only',
          'Programming Language :: Python :: 3.7',
          'Topic :: Text Processing :: Fonts',
          'Topic :: Multimedia :: Graphics',
          'Topic :: Multimedia :: Graphics :: Graphics Conversion',
      ],
      )
