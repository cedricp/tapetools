#pragma once

#include <stdint.h>
#include <math.h>


class NoiseGeneratorBase {
    uint64_t u,v,w;
    bool is_seed_init = false;
    
public:
    NoiseGeneratorBase(){
        is_seed_init = false;
    }
    virtual ~NoiseGeneratorBase(){}

    virtual double sample() = 0;

    uint64_t randq64_uint64()
    {    
        if (!is_seed_init){
            is_seed_init = true;
            v = 4101842887655102017LL;
            w = 1;
            u = 1ULL ^ v; 
            randq64_uint64();
            v = u; 
            randq64_uint64();
            w = v; 
            randq64_uint64();
        }
        
        u = u * 2862933555777941757LL + 7046029254386353087LL;
        v ^= v >> 17; 
        v ^= v << 31; 
        v ^= v >> 8;
        w = 4294957665U*(w & 0xffffffff) + (w >> 32);
        uint64_t x = u ^ (u << 21); 
        x ^= x >> 35; 
        x ^= x << 4;
        return (x + v) ^ w;
    }

    double randq64_double()
    {
        return 5.42101086242752217E-20 * randq64_uint64();
    }


};

class WhiteNoiseGenerator : public NoiseGeneratorBase {
public:
    WhiteNoiseGenerator() : NoiseGeneratorBase() {
    }

    double sample() override {
        return 2.0 * randq64_double() - 1.0;
    }
};

class BrownNoiseGenerator : public WhiteNoiseGenerator {
    double lastOutput;
public:
    BrownNoiseGenerator() : WhiteNoiseGenerator() {
        lastOutput = 0.0;
    }

    double sample() override {
        double white = WhiteNoiseGenerator::sample();
        lastOutput = lastOutput - (0.025 * (lastOutput - white));
        return lastOutput; 
    }
};

class PinkNoiseGenerator : public WhiteNoiseGenerator {
    double b0, b1, b2, b3, b4, b5, b6;  
public:
    PinkNoiseGenerator() : WhiteNoiseGenerator() {
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0;
    }
    
    double sample() override {
        double white = WhiteNoiseGenerator::sample();
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
        b6 = white * 0.115926;
        return pink * 0.11; // Gain compensation
    }
};