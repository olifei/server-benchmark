while true
do
    cd /root/test/testfile/linpack
    ./runme_xeon64

    cd /root/test/testfile/stream 
    for i in {1..10}
    do
        ./stream_avx512_mcmodel_1600M
    done

done
