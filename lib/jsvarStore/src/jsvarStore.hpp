#ifndef JSVARSTORE_H
#define JSVARSTORE_H

#include <Arduino.h>

#include <map>

#include "Stream.h"



class JsvarStore {

public:
    JsvarStore();
    ~JsvarStore();

    //updates with new data from stream. Parses at most one variable before returning. Returns the name of the parsed variable.
    String handleChar(const char &c);

    //returns all vars in the source formatting
    String dumpVars() const;

    //return the content of a specific var. Returns empty string if error or var not found.
    String getVar(const String varName) const;

    //reset the parser
    void reset();

protected:

private:

    //semaphore for data access
    SemaphoreHandle_t mMutex;

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

    //debug
    unsigned long timeTaken;

};



#endif
