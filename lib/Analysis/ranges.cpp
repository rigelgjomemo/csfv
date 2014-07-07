#include "llvm/Analysis/ranges.h"
#include <algorithm>
#include <sstream>

namespace ranges {
    
    Range::Range() : bottom(0), top(0) { }
    Range::Range(int bottom, int top) : bottom(bottom), top(top) { }
        
    Range Range::operator+(Range &that){
        Range r(bottom+that.getBottom(), top+that.getTop());
        return r;
    }
    Range Range::operator-(Range &that){
        Range r(bottom-that.getTop(), top-that.getBottom());
        return r;
    }
    Range Range::operator*(Range &that){
        int num1 = bottom*that.getBottom();
        int num2 = bottom*that.getTop();
        int num3 = top*that.getBottom();
        int num4 = top*that.getTop();
        
        int min = std::min( std::min(num1,num2), std::min(num3,num4) );
        int max = std::max( std::max(num1,num2), std::max(num3,num4) );

        Range r(min, max);
        return r;
    }
    Range Range::operator+(int num){
        Range r(bottom+num, top+num);
        return r;
    }
    Range Range::operator-(int num){
        Range r(bottom-num, top-num);
        return r;
    }
    Range Range::operator*(int num){
        Range r(bottom*num, top*num);
        return r;
    }
    Range Range::operator/(int num){
        Range r(bottom/num, top/num);
        return r;
    }

    
    int Range::getBottom() const { return bottom; }
    int Range::getTop() const { return top; }

    void Range::dump() {
        llvm::errs() << *this << "\n";
    }

    std::string Range::getAnnotation(std::string name){
        std::stringstream ss;
        ss << "assert " << name <<" >= "<<bottom<<" && "<< name <<" <= " <<top;
        return ss.str();
    }
}
llvm::raw_ostream & ranges::operator<<(llvm::raw_ostream &os, Range &r) {
    os << '[' << r.getBottom() << ',' << r.getTop() << ']';
    return os;
}
