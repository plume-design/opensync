#!/usr/bin/env sh

out="pcap_map.c"
generate_table_entries()
{
    in=$1
    out=$2
    awk -v output=${out} '
    {
        if (!($0 ~ /static const unsigned char/)) { next }
        split($5, a, /\[|\]/)
        pkt = a[1]
        len = a[2]
        print "    { .name = \"" pkt "\", .pkt = " pkt ", .len = " len " }," >> output
    }' ${in}
}



echo "struct pcap_map"               >  ${out}
echo "{"                             >> ${out}
echo "    const char *name;"         >> ${out}
echo "    const unsigned char *pkt;" >> ${out}
echo "    size_t len;"               >> ${out}
echo "} pmap[] ="                    >> ${out}
echo "{"                             >> ${out}

generate_table_entries "pcap.c" ${out}

echo "};"                            >> ${out}
