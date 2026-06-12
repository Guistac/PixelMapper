#include <iostream>
#include <chrono>
#include <string>

class Timer {
public:
    Timer(std::string n){
        name = n;
        start_time = std::chrono::high_resolution_clock::now();
    }

    ~Timer() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        std::cout << "[Timer] " << name << ": " << duration << " us (" << duration / 1000.0 << " ms)" << std::endl;
    }

private:
    std::string name;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
};

#define PROFILE_SCOPE(name) Timer timer__##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)
