setenv_arm_all()
{
        source /Applications/Momentics.app/bbndk-env_10_3_0_698.sh
        # Add internal libs
        export CFLAGS="$CFLAGS $MAKEFLAGS -I$GLOBAL_OUTDIR/include -L$GLOBAL_OUTDIR/lib -I/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/src -lintl"

        export CPP="$QNX_HOST/usr/bin/ntoarmv7-cpp-4.6.3"
        export CXX="$QNX_HOST/usr/bin/ntoarmv7-g++-4.6.3"
        export CXXCPP="$QNX_HOST/usr/bin/ntoarmv7-cpp-4.6.3"
        export CC="$QNX_HOST/usr/bin/ntoarmv7-gcc-4.6.3"
        export LD=$QNX_HOST/usr/bin/ntoarmv7-ld
        export AR=$QNX_HOST/usr/bin/ntoarmv7-ar
        export AS=$QNX_HOST/usr/bin/ntoarmv7-as
        export NM=$QNX_HOST/usr/bin/ntoarmv7-nm
        export RANLIB=$QNX_HOST/usr/bin/ntoarmv7-ranlib
        export LDFLAGS="-L$QNX_TARGET/armle-v7/usr/lib/"

        export CPPFLAGS=$CFLAGS
        export CXXFLAGS=$CFLAGS
}

setenv_x86_all()
{
        source /Applications/Momentics.app/bbndk-env_10_3_0_698.sh
        # Add internal libs
        export CFLAGS="$CFLAGS $MAKEFLAGS -I$GLOBAL_OUTDIR/include -L$GLOBAL_OUTDIR/lib -I/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/x86-qnx/include -lintl"

        export CPP="$QNX_HOST/usr/bin/ntox86-cpp-4.6.3"
        export CXX="$QNX_HOST/usr/bin/ntox86-g++-4.6.3"
        export CXXCPP="$QNX_HOST/usr/bin/ntox86-cpp-4.6.3"
        export CC="$QNX_HOST/usr/bin/ntox86-gcc-4.6.3"
        export LD=$QNX_HOST/usr/bin/ntox86-ld
        export AR=$QNX_HOST/usr/bin/ntox86-ar
        export AS=$QNX_HOST/usr/bin/ntox86-as
        export NM=$QNX_HOST/usr/bin/ntox86-nm
        export RANLIB=$QNX_HOST/usr/bin/ntox86-ranlib
    #    export LDFLAGS="-L$QNX_TARGET/armle-v7/usr/lib/"

        export CPPFLAGS=$CFLAGS
        export CXXFLAGS=$CFLAGS
}

mkdir build
mkdir build/x86-qnx
mkdir build/arm-qnx



# ---------------------------------------------------------------
# compile for VM

# setenv_x86_all

source /Applications/Momentics.app/bbndk-env_10_3_0_698.sh

make clean


./configure --host=i486-pc-nto-qnx8.0.0 --disable-shared --enable-static --with-gcrypt-prefix="/Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/" 


export CFLAGS="$CFLAGS $MAKEFLAGS -I$GLOBAL_OUTDIR/include -L$GLOBAL_OUTDIR/lib -I/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/x86-qnx/include -L/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/x86-qnx/lib -L/Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/build/x86-qnx -lintl"


qcc -set-default -V4.6.3,gcc_ntox86_cpp

make


# ---------------------------------------------------------------
# compile for device

source /Applications/Momentics.app/bbndk-env_10_3_0_698.sh

make clean


./configure --host=arm-unknown-nto-qnx8.0.0eabi --disable-shared --enable-static --with-gcrypt-prefix="/Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/" 


export CFLAGS="$CFLAGS $MAKEFLAGS -I$GLOBAL_OUTDIR/include -L$GLOBAL_OUTDIR/lib -I/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/arm-qnx/include -L/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/arm-qnx/lib -L/Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/build/arm-qnx -lintl"


qcc -set-default -V4.6.3,gcc_ntoarmv7le_cpp

make
