# Required to generate MOC
target_sources(
        flameshot
        PRIVATE buttonhandler.h
        capturebutton.h
        capturetoolbutton.h
        capturewidget.h
        colorpicker.h
        hovereventfilter.h
        overlaymessage.h
        selectionwidget.h
        magnifierwidget.h
        notifierbox.h
        ocrpanel.h
        pretoolbar.h
        modificationcommand.h)

target_sources(
        flameshot
        PRIVATE buttonhandler.cpp
        capturebutton.cpp
        capturetoolbutton.cpp
        capturewidget.cpp
        colorpicker.cpp
        hovereventfilter.cpp
        overlaymessage.cpp
        notifierbox.cpp
        ocrpanel.cpp
        pretoolbar.cpp
        selectionwidget.cpp
        magnifierwidget.cpp
        modificationcommand.cpp)
