#!/usr/bin/env bash

# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


comments()
{
    # remove empty /* */ comments and
    # fmt comments to separate lines
    sed 's,\*/ *;,\*/,g;s,/\*  *\*/,,g;s,/\*\*/,,;s,\*/ ,*/\n,g;s, /\*,\n\n/*,g'
}

expand_tmpl()
{
    F=$1
    OUT=${F/.tmpl/}
    TMP1=$F.tmp1.c
    TMP2=$F.tmp3.c
    TMP3=$F.tmp2.c
    NL=$'\n'
    # find template header
    INC=$(sed -n '/#include.*_template_/{s/^.*["<]\(.\+\)[">].*$/\1/p;q}' $F)
    INCF=$(find $(dirname $(dirname $F)) -name $INC)
    grep -v '#include' $INCF > $TMP1
    grep '#include' $INCF > $TMP2
    sed -n '/_TEMPLATE_/,/)/{p;/)/q}' $F >> $TMP1
    # preprocess template
    gcc -E -P -CC -nostdinc $TMP1 | comments > $TMP3
    echo "// generated from $(basename $F)" > $OUT
    # replace template includes and template macro call
    sed "/#include.*\.tmpl/s/\.tmpl//" < $F | \
        sed "/#include.*_template_/{r ${TMP2}${NL};d}" | \
        sed "/_TEMPLATE_[^)]*$/,/)/{/(/b;d}" | \
        sed "/_TEMPLATE_/{r ${TMP3}${NL}d}" >> $OUT
    # reformat according to defined coding style
    clang-format -fallback-style=none -style=file -i $OUT
    # add empty line between functions and structs
    sed -i 's/^}$/}\n/;s/^};$/};\n/' $OUT
    # merge ajdacent string literals
    sed -i ':A /"$/{N;/"\n *"/{s/"\n *"//;bA}}' $OUT
    # remove superfluous semicolons
    sed -i '/^ *; *$/d' $OUT
    # final reformat
    clang-format -fallback-style=none -style=file -i $OUT
    rm -f $TMP1 $TMP2 $TMP3
    echo $OUT
}

for FF in $(find . -name '*.tmpl.[ch]'); do
    expand_tmpl $FF
done

