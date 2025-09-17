#/bin/sh

# Processing script to optionally reduce filesystem use by miniying, gzipping and preparing index.htm for embedding in code.
# Please see readme.md for more information.

# Requires xdd which is part of the VIM package
# Requires npm
#   sudo apt install npm
#   ln -s /usr/bin/nodejs /usr/bin/node
# Requires html-minifier
#   sudo npm install html-minifier -g

echo "\n"

HTML_FILE=$1.html
HTM_FILE=$1.htm
HTM_GZ_FILE=$HTM_FILE.gz
HTM_H_FILE=$1_htm.h

set -e # exit on first error

if [ ! -f "$HTML_FILE" ]; then
  echo "File '$HTML_FILE' does not exist or is not a regular file."
  exit 1
fi

html-minifier \
 --case-sensitive \
 --collapse-boolean-attributes \
 --collapse-whitespace \
 --minify-css true \
 --minify-js true \
 --process-conditional-comments \
 --remove-attribute-quotes \
 --remove-comments \
 --remove-empty-attributes \
 --remove-optional-tags \
 --remove-redundant-attributes \
 --remove-script-type-attributes \
 --remove-style-link-type-attributes \
 -o $HTM_FILE \
 $HTML_FILE

if [ $? -ne 0 ]
then
  echo "Error minifying index.htm"
  exit 1
fi

if [ -e $HTM_GZ_FILE ]
then
  rm $HTM_GZ_FILE
fi

gzip $HTM_FILE
if [ $? -ne 0 ]
then
  echo "Error gzipping minified $HTM_FILE"
  exit 1
fi

echo '// WARNING: Auto-generated file. Please do not modify by hand.' > $HTM_H_FILE
echo '// This file is an embeddable version of the gzipped index.htm file.' >> $HTM_H_FILE
echo '// To update it, please rerun the `reduce_index.sh` script located in the `extras` subfolder' >> $HTM_H_FILE
echo '// then recompile the sketch after each change to the `index.html` file.' >> $HTM_H_FILE
xxd -i $HTM_GZ_FILE >> $HTM_H_FILE
if [ $? -ne 0 ]
then
  echo "Error creating include file from $HTM_GZ_FILE"
  exit 1
fi

rm $HTM_GZ_FILE

echo Reduce complete.

