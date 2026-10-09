#undef NDEBUG
#include <assert.h>
#include "chain_arrow.h"
int main() {
    chain_arrow::init();
    const int positions[]={14,62,126,190,254,262};
    for(int left: positions) {
        unsigned lit=0;
        for(unsigned word=0;word<4096;++word) {
            unsigned m=chain_arrow::mask(word,left);
            lit+=(m&1)+((m>>1)&1);
            for(unsigned bit=0;bit<16;++bit) {
                unsigned clock=word*16+bit;
                auto v=chain_arrow::samples[word%64][m][2*bit];
                assert(((v>>6)&7)==((clock+16)/128)%8);
                assert(bool(v&1024)==(clock%512==511));
                assert(bool(v&2048)==(clock%128>=100 && clock%128<104));
                assert(chain_arrow::samples[word%64][m][2*bit+1]==(v|512));
            }
        }
        assert(left>=256?lit==0:lit>0);
    }
    assert(chain_arrow::pixel(41,15,14));
    assert(!chain_arrow::pixel(42,15,14));
}
