#include "jsvarStore.hpp"

//include for yield on esp32
#include <esp32-hal.h>

JsvarStore::JsvarStore()
    : mState(0)
    , mCounter(0)
{
    mMutex = xSemaphoreCreateMutex();
}

JsvarStore::~JsvarStore()
{
    vSemaphoreDelete(mMutex);
}

String JsvarStore::handleChar(const char &c)
{

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
            if(mVarName.charAt(0) != 'h') //special treatment of history download
            {
                if( xSemaphoreTake( mMutex, (TickType_t) 5 ) )
                {
                    auto var = mVars.find(mVarName);
                    if(var == mVars.end())
                    {
                        mVars.insert(std::pair<String,String>(String(mVarName), String(mVarContent))); //insert in our stored data
                    }
                    else
                    {
                        var->second = String(mVarContent);
                    }
                    xSemaphoreGive(mMutex);
                }
            }
            
            String varName = mVarName;
            reset();
            
            return varName;
        }
        else //error case
        {
            reset();
        }
    }

    return String();
}


String JsvarStore::dumpVars() const
{
    String dump((char*)0); //do not reserve anything at first
    dump.reserve(2000); //then reserve huge block, this should fit everything

    if( xSemaphoreTake( mMutex, (TickType_t) 50 ) )
    {
        auto it = mVars.cbegin();
        while(it != mVars.cend())
        {
            //reconstruct the original line syntax (without temporary strings involved)
            dump += "var ";
            dump += it->first;
            dump += "=";
            dump += it->second;
            dump += ";\r\n";

            ++it;
        }
        xSemaphoreGive(mMutex);
    }

    return dump;
}


String JsvarStore::getVar(const String varName) const
{
    String res;
    if( xSemaphoreTake( mMutex, (TickType_t) 50 ) )
    {
        auto var = mVars.find(varName);
        if(var != mVars.cend())
        {
            res = var->second; //return a copy of the data
        }
        xSemaphoreGive(mMutex);
    }
    return res;
}

void JsvarStore::reset()
{
    mState = 0;
    mCounter = 0;
    mVarName.clear();
    mVarContent.clear();
    timeTaken = 0;
}

