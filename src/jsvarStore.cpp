#include "jsvarStore.hpp"

//include for yield on esp32
#include <esp32-hal.h>

JsvarStore::JsvarStore(Stream &stream)
    : mStream(stream)
    , mState(0)
    , mCounter(0)
{

}

bool JsvarStore::update()
{
    bool retval = false;
    while(mStream.available())
    {
        char c = mStream.read();

        if(mState == 0) //search for "var " with the space
        {
            const char var[] = "var ";
            if(mCounter <= 4 && var[mCounter] == c)
            {
                mCounter ++;

                if(mCounter == 4) //token found sucessfully
                {
                    mCounter = 0;
                    mState = 1;
                }
            }
            else //error case
            {
                reset();
            }
            
        }
        else if(mState == 1) //parse variable name until '='
        {
            if(c == '=') //end of var name
            {
                mState = 2;
                mCounter = 0;
            }
            else if(mCounter < 10) //max var name length is 10
            {
                mVarName += c;
                mCounter ++;
            }
            else //error case
            {
                reset();
            }
        }
        else if(mState == 2) //expect a quotation mark or a [
        {
            if(c == '\"' || c == '[')
            {
                mState = 3;
                mVarContent += c;
            }
            else //error case
            {
                reset();
            }
        }
        else if(mState == 3) //parse content until '\"' or ']'
        {
            if((c == '\"' && mVarContent.charAt(0) == '\"') || (c == ']' && mVarContent.charAt(0) == '[')) //end of content
            {
                mState = 4;
                mCounter = 0;
                mVarContent += c;
            }
            else if(mCounter < 300) //max content length is 300
            {
                mVarContent += c;
                mCounter ++;
            }
            else //error case
            {
                reset();
            }
        }
        else if(mState == 4) //expect semicolon
        {
            if(c == ';') //line was valid, commit
            {
                mVars.insert(std::pair<String,String>(String(mVarName), String(mVarContent))); //insert/update in our stored data
                retval = true;
                reset();
            }
            else //error case
            {
                reset();
            }

            yield(); //yield at least after every line
        }

    }
    return retval;
}


String JsvarStore::dumpVars() const
{
    String dump;
    auto it = mVars.cbegin();
    while(it != mVars.cend())
    {
        //reconstruct the original line syntax
        dump += "var " + it->first + "=" + it->second + ";\r\n";

        ++it;
    }

    return dump;
}


String JsvarStore::getVar(const String varName) const
{
    auto var = mVars.find(varName);
    if(var != mVars.cend())
    {
        return String(var->second); //return a copy of the data
    }
    return String();
}

void JsvarStore::reset()
{
    mState = 0;
    mCounter = 0;
    mVarName.clear();
    mVarContent.clear();
}

