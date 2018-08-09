FROM ebbrt/hosted:centos7
ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/lib:/usr/local/lib64:/usr/lib64
WORKDIR /tmp

# upgrade cmake
RUN yum remove cmake -y
RUN wget https://cmake.org/files/v3.6/cmake-3.6.2.tar.gz && tar -zxf cmake-3.6.2.tar.gz
WORKDIR /tmp/cmake-3.6.2 
RUN ./bootstrap && gmake -j && make install

RUN /bin/bash
ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/lib:/usr/local/lib64:/usr/lib64

# build dependencies 
WORKDIR /tmp
RUN git clone --recursive https://www.github.com/sesa/seuss  /tmp/seuss

WORKDIR /tmp/seuss/ext/librdkafka
RUN cmake . && make -j && make install && make clean

WORKDIR /tmp/seuss/ext/cppkafka
RUN yum install -y openssl-devel
RUN cmake . && make -j && make install && make clean

WORKDIR /tmp/seuss/ext/yajl
RUN cmake . && make -j && make install && make clean

WORKDIR /tmp/seuss/ext/pillowtalk
RUN yum install -y libcurl-devel
RUN cmake . && make -j && make install && make clean

WORKDIR /tmp/seuss/ext/EbbRT/libs/zookeeper
RUN cmake . && make -j && make install && make clean

# cleanup
RUN rm -rf /tmp/seuss
RUN rm -rf /tmp/cmake-3.6.2*

# install additional depends
# TODO(jmcadden): move these to the root Dockerfile (ebbrt/hosted)
RUN yum install -yq docker iproute

# seuss 
RUN git clone http://www.github.com/sesa/seuss  /root/seuss
WORKDIR /root/seuss
RUN cmake . && make -j 
