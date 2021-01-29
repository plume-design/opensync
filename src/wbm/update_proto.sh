#############################################
# update protobuf c files based on protobuf
#############################################

RELDIR=$(basename $(dirname $PWD))/$(basename $PWD)

if [ $RELDIR != "src/wbm" ]
then
    echo "Please change to src/wbm directory before executing this script"
    exit 1
fi

FNAME=wbm
INTERFACES_DIRPATH="../../interfaces"

protoc-c --c_out=. --proto_path=${INTERFACES_DIRPATH} ${INTERFACES_DIRPATH}/${FNAME}.proto
mv "${FNAME}.pb-c.c" src/
mv "${FNAME}.pb-c.h" src/

if [ $? -ne 0 ]
then
    echo "Error generating protobuf c files"
else
    echo "protobuf update successfully completed"
fi
