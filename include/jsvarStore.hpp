#ifndef JSVARSTORE_H
#define JSVARSTORE_H

#include <map>

#include "Stream.h"

class JsvarStore {

public:
    JsvarStore(Stream &stream);

    //updates with new data from stream. Returns true if anything was received.
    bool update();

    //returns all vars in the source formatting
    String dumpVars() const;

    //return the content of a specific var. Returns empty string if error or var not found.
    String getVar(const String varName) const;

protected:

    void reset();

private:
    //holds a reference to the Stream to read input from
    Stream &mStream;

    //hold all the data as Strings in a map for easy access
    std::map<String,String> mVars;

    //holds the current state of the parser
    uint8_t mState;

    //holds the chars parsed in the current state
    uint8_t mCounter;
    
    //holds the currently parsed variable name
    String mVarName;

    //holds the content parsed so far
    String mVarContent;

};



#endif
