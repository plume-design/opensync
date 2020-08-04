###############################################################################
# update protobuf c files
###############################################################################

RELDIR=$(basename $(dirname $PWD))/$(basename $PWD)

if [ $RELDIR != "lib/stats_pub" ]
then
    echo "Please cd to src/lib/stats_pub folder"
    exit 1
fi

FNAME=stats_pub
/usr/local/bin/protoc-c --c_out=. --proto_path=../../../interfaces ../../../interfaces/${FNAME}.proto
mv "${FNAME}.pb-c.c" src/
mv "${FNAME}.pb-c.h" src/

if [ $? -ne 0 ]
then
    echo "Error generating protobuf c files"
else
    echo "protobuf update successfully completed"
fi
