cd ../testfile/linpack
./runme_xeon64
    
cd ../stream
for i in {1..10}
do
    ./stream_avx512_mcmodel_1600M
done
