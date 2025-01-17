#pragma once

#include "engine/core/base/object.h"
#include "engine/core/util/StringUtil.h"
#include "build_listener.h"

namespace Echo
{
    class EditorSettings : public Object
    {
        ECHO_VIRTUAL_CLASS(EditorSettings, Object)
        
    public:
		EditorSettings() {}
        virtual ~EditorSettings() {}
        
        // set
        virtual void setOutputDir(const String& outputDir) {}
        
        // build
        virtual void build() {}
        
        // get final result path
        virtual String getFinalResultPath() { return StringUtil::BLANK; }
        
    public:
        // log
        void setListener(BuildListener* listener) { m_listener = listener; }
        void log(const char* formats, ...);
        
    protected:
        BuildListener*    m_listener = nullptr;
    };
}
