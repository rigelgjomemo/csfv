#ifndef ranges_h
#define ranges_h

#include "llvm/Support/raw_ostream.h"
#include <string>
#include <cstdlib>

namespace ranges {
    
    class Range{
    public:
        Range();
        Range(int bottom, int top);
        
    public:
        Range operator+(Range &that);
        Range operator-(Range &that);
        Range operator*(Range &that);
        Range operator+(int num);
        Range operator-(int num);
        Range operator*(int num);
        Range operator/(int num);

        
    public:
        int getBottom() const;
        int getTop() const;
        void dump();

        std::string getAnnotation(std::string name);

    private:
        int bottom;
        int top;
        friend llvm::raw_ostream &operator<<(llvm::raw_ostream &, Range &);
    };
}

#endif
