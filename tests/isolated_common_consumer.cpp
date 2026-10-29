// Public common decoder only: no PICO packet/backend type dependency.
#include "monaka/protocol/v2/codec.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2)return 2;std::ifstream input(argv[1]);if(!input)return 3;
    std::string line;size_t count=0;
    while(std::getline(input,line)) {
        monaka::protocol::v2::Envelope envelope;monaka::protocol::v2::Error error;
        if(!monaka::protocol::v2::DecodeEnvelope(reinterpret_cast<const uint8_t*>(line.data()),line.size(),envelope,error)) {std::cerr<<error.message<<'\n';return 1;}
        ++count;
    }
    if(!count)return 4;std::cout<<"PASS common v2 decoded="<<count<<'\n';return 0;
}
