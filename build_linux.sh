#!/bin/sh
docker build . -f linux.Dockerfile -t fccorebuilder
id=$(docker create fccorebuilder)
docker cp $id:/FrameCapturer/FrameCapturer/Assets/UTJ/FrameCapturer/Plugins/x86_64/libfccore.so ./FrameCapturer/Assets/UTJ/FrameCapturer/Plugins/x86_64/
docker rm -v $id