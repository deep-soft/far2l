# 2023-12-16 09:00
name: act-Release -Mac

on:
#  push
  workflow_dispatch:
    inputs:
      PLUGINS_EXTRA: 
        description: 'PLUGINS_EXTRA'
        type: boolean
        default: true
      make_archive_all:
        description: 'Archive ALL'
        type: boolean
        default: false
      run_tests:
      # Execute tests defined by the CMake configuration.
        description: 'run tests'
        type: boolean
        default: false
      MULTIARC_PDF:
        description: MULTIARC_PDF
        type: boolean
        default: false
        required: true
      MULTIARC_PDF_PARAMS:
        description: MULTIARC_PDF_PARAMS
        default: "multiarc/src/ArcPlg.cpp"
        required: true
      SED_STUFF:
        description: SED_STUFF
        type: boolean
        default: false
        required: true
      SED_STUFF_PARAMS:
        description: SED_STUFF_PARAMS
        default: "filename_to_cat"
        required: true
      DEBUG_MODE_1:
        description: DEBUG_MODE_1
        default: "N"
        required: true
      DEBUG_MODE_2:
        description: DEBUG_MODE_2
        default: "N"
        required: true

env:
  # Customize the CMake build type here (Release, Debug, RelWithDebInfo, etc.)
  BUILD_TYPE: Release
  PROG_NAME: far2l
  PROG_VERSION: "v1.0.0"

jobs:
  build:
    name: ${{ matrix.job.name }} (${{ matrix.job.os }})
    runs-on: ${{ matrix.job.os }}
    strategy:
      fail-fast: false
      matrix:
        job:
          #- { name: "minimal"    , os: ubuntu-20.04 , dependencies: "-minimal"     , options: ""             }
          #- { name: "withWX"     , os: ubuntu-20.04 , dependencies: "-smb-nfs-neon", options: "-DUSEWX=yes"  }
          #- { name: "noWX"       , os: ubuntu-20.04 , dependencies: "-smb-nfs-neon", options: "-DUSEWX=no"   }
          #- { name: "withPYTHON" , os: ubuntu-20.04 , dependencies: "-smb-nfs-neon", options: "-DPYTHON=yes" }
          #- { name: "minimal"    , os: ubuntu-latest, dependencies: "-minimal"     , options: ""             }
          #- { name: "withWX"     , os: ubuntu-latest, dependencies: "-smb-nfs-neon", options: "-DUSEWX=yes"  }
          #- { name: "noWX"       , os: ubuntu-latest, dependencies: "-smb-nfs-neon", options: "-DUSEWX=no"   }
          #- { name: "withPYTHON" , os: ubuntu-latest, dependencies: "-smb-nfs-neon", options: "-DPYTHON=yes" }
          - { name: "minimal"    , os: macos-latest , dependencies: "-minimal"     , options: ""             }
          - { name: "withWX"     , os: macos-latest , dependencies: "-smb-nfs-neon", options: "-DUSEWX=yes"  }
          - { name: "noWX"       , os: macos-latest , dependencies: "-smb-nfs-neon", options: "-DUSEWX=no"   }
          - { name: "withPYTHON" , os: macos-latest , dependencies: "-smb-nfs-neon", options: "-DPYTHON=yes" }

    steps:
    - uses: deep-soft/checkout@v3

    - name: set env.SED_EXE and macOS install gnu-sed
      #if: ${{ startsWith(matrix.job.os, 'macos') }}
      shell: bash
      run: |
        SED_EXE=$(which sed);
        echo "SED_EXE=$SED_EXE";
        if [[ ${{ matrix.job.os }} =~ "macos" ]]; then
          brew install gnu-sed;
          export PATH="/usr/local/opt/gnu-sed/libexec/gnubin:$PATH";
          SED_EXE=$(which sed);
        fi
        echo "SED_EXE=$SED_EXE";
        echo "SED_EXE=$SED_EXE" >> $GITHUB_ENV;
        #ls -la $SED_EXE;
        #export sed=$SED_EXE
        #which sed

    - name: MULTIARC_PDF patch 1
      if: ${{ inputs.MULTIARC_PDF }}
      #continue-on-error: true
      shell: bash
      env:
        DEBUG_MODE_1: ${{ inputs.DEBUG_MODE_1 }}
        DEBUG_MODE_2: ${{ inputs.DEBUG_MODE_2 }}
      run: |
        if [[ -f bins/multiarc-pdf-patch.sh ]]; then
          bash bins/multiarc-pdf-patch.sh "bins/sed-stuff-multiarc-pdf.txt" "${{ inputs.MULTIARC_PDF_PARAMS }}"
        fi

    - name: sed-stuff
      if: ${{ inputs.SED_STUFF }}
      continue-on-error: true
      shell: bash
      env:
        DEBUG_MODE_1: ${{ inputs.DEBUG_MODE_1 }}
        DEBUG_MODE_2: ${{ inputs.DEBUG_MODE_2 }}
      run: |
        bash bins/sed-stuff.sh "_" "${{ inputs.SED_STUFF_PARAMS }}"

    - name: extra plugins
      if: ${{ inputs.PLUGINS_EXTRA }}
      shell: bash
      run: |
        PLUGINS_EXTRA="true";
        if [[ "$PLUGINS_EXTRA" == "true" ]]; then
          # old plugins, removed from base git
          # remove existing extra plugins
          # rm -R far2l-netcfgplugin;
          # rm -R far2l-processes;
          # rm -R far2l-sqlplugin;
          # sed -ibak 's!add_subdirectory (far2l-netcfgplugin)!# add_subdirectory (far2l-netcfgplugin)!' CMakeLists.txt;
          # sed -ibak 's!add_subdirectory (far2l-processes)!# add_subdirectory (far2l-processes)!' CMakeLists.txt;
          # sed -ibak 's!add_subdirectory (far2l-sqlplugin)!# add_subdirectory (far2l-sqlplugin)!' CMakeLists.txt;
          # rm -R far2l-EditWrap;
          # rm -R far2l-jumpword;
          # sed -ibak 's!add_subdirectory (far2l-EditWrap)!# add_subdirectory (far2l-EditWrap)!' CMakeLists.txt;
          # sed -ibak 's!add_subdirectory (far2l-jumpword)!# add_subdirectory (far2l-jumpword)!' CMakeLists.txt;

          if [[ ${{ matrix.job.os }} =~ "macos" ]]; then          
            extra_plugin_list='netcfgplugin sqlplugin';
          else
            extra_plugin_list='netcfgplugin sqlplugin processes';
          fi
          # add new extra plugins
          for plug in $extra_plugins_list ; do
            # git clone --depth 1 https://github.com/VPROFi/$plug.git && \
            git clone --depth 1 https://github.com/deep-soft/far2l-$plug.git && \
            ( echo "mv far2l-$plug $plug" && \
              mv far2l-$plug $plug && \
              cd $plug && \
              find . -mindepth 1 -name 'src' -prune -o -exec rm -rf {} + && \
              mv src/* . && rm -rf src )
            echo "add_subdirectory($plug)" >> CMakeLists.txt
          done
          # far2l-EditWrap far2l-jumpword
          for plug in EditWrap jumpword ; do
            git clone --depth 1 https://github.com/deep-soft/far2l-$plug.git && \
            ( echo "mv far2l-$plug $plug" && \
              mv far2l-$plug $plug )
            echo "add_subdirectory($plug)" >> CMakeLists.txt
          done
          grep "add_subdirectory" CMakeLists.txt
        fi

    - name: get os version
      shell: bash
      run: |
        set -x
        if [[ ${{ matrix.job.os }} =~ "macos" ]]; then
          _os_name_="macos";
          _os_version_=$(sw_vers | grep ProductVersion | cut -d':' -f2);
          echo "OS_VERSION=$_os_name_-$_os_version_" >> $GITHUB_ENV;
        else
          _os_release_file_="/etc/os-release"
          if [ -f $_os_release_file_ ]; then
            echo "-1-"
            cat "$_os_release_file_"
            echo "-2-"
               _os_name_=$(cat $_os_release_file_ | grep         "^ID=" | head -n 1 | awk -F = '{print $2}' | tr -d \")
            _os_version_=$(cat $_os_release_file_ | grep "^VERSION_ID=" | head -n 1 | awk -F = '{print $2}' | tr -d \")
            echo "OS_VERSION=$_os_name_-$_os_version_" >> $GITHUB_ENV
          fi
        fi

    - name: print os version
      shell: bash
      run: |
        echo "OS_VERSION=${{ env.OS_VERSION }}"

    - name: set program version
      shell: bash
      run: |
        set -x
        _version_=$(cat packaging/version)
        echo "PROG_VERSION=v$_version_" >> $GITHUB_ENV

    - name: print program version
      shell: bash
      run: |
        echo "PROG_VERSION=${{ env.PROG_VERSION }}"

    - name: add dependencies
      shell: bash
      run: |
        cp dependencies.txt dependencies-smb-nfs-neon.txt
        echo "" >> dependencies-smb-nfs-neon.txt
        echo "libsmbclient-dev" >> dependencies-smb-nfs-neon.txt
        echo "libnfs-dev" >> dependencies-smb-nfs-neon.txt
        echo "libneon27-dev" >> dependencies-smb-nfs-neon.txt
        # echo "libneon*-dev" >> dependencies-smb-nfs-neon.txt

    - name: Dependencies
      run: |
        if [[ ${{ matrix.job.os }} =~ "macos" ]]; then
          brew bundle -v
          export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$(brew --prefix)/opt/openssl/lib/pkgconfig:$(brew --prefix)/opt/libarchive/lib/pkgconfig"
          echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH" >> $GITHUB_ENV
        else
          sudo apt-get update ; sudo apt-get -y install $(cat dependencies${{ matrix.job.dependencies }}.txt)
        fi

    - name: Create Build Environment
      # Some projects don't allow in-source building, so create a separate build directory
      # We'll use this as our working directory for all subsequent commands
      run: cmake -E make_directory ${{github.workspace}}/build

    - name: Configure CMake
      # Use a bash shell so we can use the same syntax for environment variable
      # access regardless of the host operating system
      shell: bash
      working-directory: ${{github.workspace}}/build
      # Note the current convention is to use the -S and -B options here to specify source 
      # and build directories, but this is only available with CMake 3.13 and higher.  
      # The CMake binaries on the Github Actions machines are (as of this writing) 3.12
      run: cmake $GITHUB_WORKSPACE -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=$BUILD_TYPE ${{ matrix.job.options }}

    - name: Build
      working-directory: ${{github.workspace}}/build
      shell: bash
      # Execute the build.  You can specify a specific target with "--target <NAME>"
      run: cmake --build . --config $BUILD_TYPE

    - name: Remove extra languages
      working-directory: ${{github.workspace}}/build
      shell: bash
      run: |
        #touch 0-all-lng-hlf.txt
        #find . *.lng >> 0-all-lng-hlf.txt
        #find . *.hlf >> 0-all-lng-hlf.txt
        #lng_hlf_no_eng=$(grep.exe -i -v "eng\.\|e\.\|en\." 0-all-lng-hlf.txt)
        
        lng_hlf_no_eng=$(find . -name *.lng -o -name *.hlf | grep -i -v "eng\.\|en\.\|calce\.|colorere\.")
        for lng_hlf in $lng_hlf_no_eng ; do
          echo "$lng_hlf";
          rm "$lng_hlf";
        done

    - name: Build package
      working-directory: ${{github.workspace}}/build
      shell: bash
      run: |
        cmake --build . --target package
        mv ${{ env.PROG_NAME }}*.deb    ${{ env.PROG_NAME }}-${{ env.PROG_VERSION }}-${{ env.OS_VERSION }}-${{ matrix.job.name }}.deb
        mv ${{ env.PROG_NAME }}*.tar.gz ${{ env.PROG_NAME }}-${{ env.PROG_VERSION }}-${{ env.OS_VERSION }}-${{ matrix.job.name }}.tar.gz

    - name: File Tree
      working-directory: ${{github.workspace}}/build
      shell: bash
      run: tree

#    - name: Packing Artifact
#      uses: deep-soft/upload-artifact@v3
#      with:
#        name: ${{ env.PROG_NAME }}-${{ env.PROG_VERSION }}-${{ matrix.job.name }}
#        path: ${{github.workspace}}/build/install

    - name: Archive Release
      uses: deep-soft/zip-release@v2
      with:
        type: 'zip'
        filename: '${{ env.PROG_NAME }}-${{ env.PROG_VERSION }}-${{ env.OS_VERSION }}-${{ matrix.job.name }}.zip'
        directory: '${{github.workspace}}/build/install'
        path: '.'
        # exclusions: '*.git* /*node_modules/* .editorconfig'
        exclusions: '*Bel.lng *Rus.lng *Cze.lng *Ger.lng *Hun.lng *Pol.lng *Spa.lng *Ukr.lng *bel.lng *rus.lng *calcb.lng *calcr.lng *colorerb.lng *colorerr.lng *editorcomp_be.lng *editorcomp_ru.lng *isrcrus.lng'
        recursive_exclusions: '*Bel.lng *Rus.lng *Cze.lng *Ger.lng *Hun.lng *Pol.lng *Spa.lng *Ukr.lng *bel.lng *rus.lng *calcb.lng *calcr.lng *colorerb.lng *colorerr.lng *editorcomp_be.lng *editorcomp_ru.lng *isrcrus.lng'
        debug: yes

    - name: Publish Release
      continue-on-error: true
      uses: deep-soft/action-gh-release@v1
      with:
        draft: true
        tag_name: ${{ env.PROG_NAME }}-${{ env.PROG_VERSION }}
        files: |
          ${{ env.ZIP_RELEASE_ARCHIVE }}
          ${{github.workspace}}/build/${{ env.PROG_NAME }}*.deb
          ${{github.workspace}}/build/${{ env.PROG_NAME }}*.tar.gz

    - name: Archive Release - All
      if: ${{ inputs.make_archive_all }}
      uses: deep-soft/zip-release@v2
      with:
        type: 'zip'
        filename: '${{ env.PROG_NAME }}-${{ env.PROG_VERSION }}-${{ env.OS_VERSION }}-${{ matrix.job.name }}-ALL.zip'
        directory: '${{github.workspace}}'
        path: '.'
        # exclusions: '*.git* /*node_modules/* .editorconfig'
        exclusions: ''
        recursive_exclusions: ''
        env_variable: ZIP_RELEASE_ARCHIVE_ALL
        debug: yes

    - name: Publish Release - All
      if: ${{ inputs.make_archive_all }}
      continue-on-error: true
      uses: deep-soft/action-gh-release@v1
      with:
        draft: true
        tag_name: ${{ env.PROG_NAME }}-${{ env.PROG_VERSION }}-ALL
        files: |
          ${{ env.ZIP_RELEASE_ARCHIVE_ALL }}

    - name: Test
      if: ${{ inputs.run_tests }}
      working-directory: ${{github.workspace}}/build
      shell: bash
      # Execute tests defined by the CMake configuration.  
      # See https://cmake.org/cmake/help/latest/manual/ctest.1.html for more detail
      run: ctest -C $BUILD_TYPE
