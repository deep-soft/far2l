#!/bin/bash
        unzip -q bins/multiarc-pdf-add.zip
        if [[ -f bins/multiarc/src/formats/pdf/pdf.cpp ]]; then
          cp bins/multiarc/src/formats/pdf/pdf.cpp multiarc/src/formats/pdf/pdf.cpp
        fi

        cat multiarc/src/formats/all.h-add-pdf-new >> multiarc/src/formats/all.h
        echo "cat multiarc/src/formats/all.h - begin"
        cat multiarc/src/formats/all.h
        echo "cat multiarc/src/formats/all.h - end"

        cat multiarc/CMakeLists.txt-add >> multiarc/CMakeLists.txt
        echo "cat multiarc/CMakeLists.txt - begin"
        cat multiarc/CMakeLists.txt
        echo "cat multiarc/CMakeLists.txt - end"

        bash bins/sed-stuff.sh $1 $2
