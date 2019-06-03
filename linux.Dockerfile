# Get the base Ubuntu image from Docker Hub
FROM ubuntu:latest

# Update apps on the base image
RUN apt-get -y update && apt-get install -y

RUN apt-get -y install build-essential git cmake autoconf libtool

RUN apt-get -y install curl libz-dev python

#openexr
RUN curl -SL https://github.com/openexr/openexr/releases/download/v2.3.0/ilmbase-2.3.0.tar.gz \
    | tar -xzC /tmp/ \
    && cd /tmp/ilmbase-2.3.0 \
    && ./configure CFLAGS=-fPIC CXXFLAGS=-fPIC --enable-static --disable-shared \
    && make all \
    && make install

RUN curl -SL https://github.com/openexr/openexr/releases/download/v2.3.0/openexr-2.3.0.tar.gz \
    | tar -xzC /tmp/ \
    && cd /tmp/openexr-2.3.0 \
    && ./configure CFLAGS=-fPIC CXXFLAGS=-fPIC --enable-static --disable-shared \
    && make all \
    && make install

#yuv
ENV LIBYUV_GIT_URL https://chromium.googlesource.com/libyuv/libyuv 
ENV LIBYUV_COMMIT_HASH 681c6c67
ENV LIBYUV_DIR /tmp/libyuv
RUN git clone $LIBYUV_GIT_URL $LIBYUV_DIR \
  && cd $LIBYUV_DIR \
  && git reset --hard $LIBYUV_COMMIT_HASH \
  # && sed -i '6 a set(CMAKE_POSITION_INDEPENDENT_CODE ON)' CMakeLists.txt \
  && mkdir -p out \
  && cd out \
  && cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="/usr" -DCMAKE_BUILD_TYPE="Release" .. \
  && cmake --build . --config Release \
  && cmake --build . --target install --config Release

#ogg
RUN curl -SL https://github.com/xiph/ogg/releases/download/v1.3.3/libogg-1.3.3.tar.xz \
    | tar -xJC /tmp/ \
    && cd /tmp/libogg-1.3.3 \
    && ./configure CFLAGS=-fPIC CXXFLAGS=-fPIC --enable-static --disable-shared \
    && make all \
    && make install

#flac
ENV LIBFLAC_GIT_URL https://github.com/xiph/flac.git 
ENV LIBFLAC_COMMIT_HASH cd03042
ENV LIBFLAC_DIR /tmp/flac
RUN git clone $LIBFLAC_GIT_URL $LIBFLAC_DIR \
    && cd $LIBFLAC_DIR \
    && git reset --hard $LIBFLAC_COMMIT_HASH \
    # && sed -i '6 a set(CMAKE_POSITION_INDEPENDENT_CODE ON)' CMakeLists.txt \
    &&  mkdir -p out \
    && cd out \
    && cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_BUILD_TYPE="Release" .. \
    && cmake --build . --config Release \
    && cmake --build . --target install --config Release

#opus
RUN curl -SL https://github.com/xiph/opus/archive/v1.3.1.tar.gz \
    | tar -xzC /tmp/ \
    && cd /tmp/opus-1.3.1 \
    && ./autogen.sh \
    && ./configure CFLAGS=-fPIC CXXFLAGS=-fPIC --enable-static --disable-shared \
    && make all \
    && make install

#vorbis
ENV LIBVORBIS_GIT_URL https://github.com/xiph/vorbis.git 
ENV LIBVORBIS_COMMIT_HASH 9eadecc
ENV LIBVORBIS_DIR /tmp/vorbis
RUN git clone $LIBVORBIS_GIT_URL $LIBVORBIS_DIR \
    && cd $LIBVORBIS_DIR \
    && git reset --hard $LIBVORBIS_COMMIT_HASH \
    # && sed -i '6 a set(CMAKE_POSITION_INDEPENDENT_CODE ON)' CMakeLists.txt \
    && mkdir -p out \
    && cd out \
    && cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_BUILD_TYPE="Release" .. \
    && cmake --build . --config Release \
    && cmake --build . --target install --config Release

#libwebm
ENV LIBWEBM_GIT_URL https://github.com/webmproject/libwebm.git 
ENV LIBWEBM_COMMIT_HASH 81de00c
ENV LIBWEBM_DIR /tmp/libwebm
RUN  git clone $LIBWEBM_GIT_URL $LIBWEBM_DIR \
    && cd $LIBWEBM_DIR \
    && git reset --hard $LIBWEBM_COMMIT_HASH \
    # && sed -i '11 a set(CMAKE_POSITION_INDEPENDENT_CODE ON)' CMakeLists.txt \
    && mkdir -p out \
    && cd out \
    && cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="/usr" -DCMAKE_BUILD_TYPE="Release" ..  \
    && make all  \
    && cp libwebm.* /usr/lib \
    && cp -r ../mkv* /usr/include \
    && cp -r ../vtt* /usr/include \
    && cp -r ../common /usr/include 

#yasm
RUN curl -SL https://github.com/yasm/yasm/archive/v1.3.0.tar.gz \
    | tar -xzC /tmp/ \
    && cd /tmp/yasm-1.3.0 \
    # && sed -i '11 a set(CMAKE_POSITION_INDEPENDENT_CODE ON)' CMakeLists.txt \
    && mkdir -p out \
    && cd out \
    && cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="/usr" -DCMAKE_BUILD_TYPE="Release" ..  \
    && make all  \
    && make install

#libvpx
RUN curl -SL https://github.com/webmproject/libvpx/archive/v1.8.0.tar.gz \
    | tar -xzC /tmp/ \
    && ls -al /tmp \
    && cd /tmp/libvpx-1.8.0 \
    && ./configure --enable-pic --enable-static --disable-shared \
    && make all \
    && make install

#libpng
RUN curl -SL https://github.com/glennrp/libpng/archive/v1.6.35.tar.gz \
    | tar -xzC /tmp/ \
    && cd /tmp/libpng-1.6.35 \
    && ./configure CFLAGS=-fPIC CXXFLAGS=-fPIC --enable-static --disable-shared \
    && make all \
    && make install

COPY . /FrameCapturer
WORKDIR /FrameCapturer/Plugin
RUN mkdir -p out \
    && cd out \
    && cmake -DCMAKE_BUILD_TYPE="Release" .. \
    && cmake -LA . | awk '{if(f)print} /-- Cache values/{f=1}' \
    && cmake --build . --config Release \
    && cmake --build . --target install --config Release

RUN ./out/dist/Test

