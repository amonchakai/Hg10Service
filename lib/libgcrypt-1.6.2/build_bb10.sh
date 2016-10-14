setenv_arm_all()
{
        source /Applications/Momentics.app/bbndk-env_10_3_0_698.sh
        # Add internal libs
        export CFLAGS="$CFLAGS $MAKEFLAGS -I$GLOBAL_OUTDIR/include -L$GLOBAL_OUTDIR/lib -I/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/src"

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
        export CFLAGS="$CFLAGS $MAKEFLAGS -I$GLOBAL_OUTDIR/include -L$GLOBAL_OUTDIR/lib -I/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/x86-qnx/include"

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


#setenv_x86_all

#make clean

#export CFLAGS="$CFLAGS $MAKEFLAGS -I$GLOBAL_OUTDIR/include -L$GLOBAL_OUTDIR/lib "

#./configure --host=i486-pc-nto-qnx8.0.0  --disable-shared --enable-static --prefix=`pwd`/build/x86-qnx

#qcc -set-default -V4.6.3,gcc_ntox86_cpp

#cd src
#make gen-posix-lock-obj

#cd ..
#make


# ---------------------------------------------------------------
# compile for device

setenv_arm_all

make clean

./configure --host=arm-unknown-nto-qnx8.0.0eabi  --disable-shared --enable-static --prefix=`pwd`/build/x86-qnx


qcc -set-default -V4.6.3,gcc_ntoarmv7le_cpp

cd src
make gen-posix-lock-obj

cd ..
make

