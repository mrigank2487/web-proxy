#!/bin/bash
#
# driver.sh - This is a simple autograder for the Proxy Lab. It does
#     basic sanity checks that determine whether or not the code
#     behaves like an iterative caching proxy. 
#
#     David O'Hallaron, Carnegie Mellon University
#     updated: 2/8/2016
#     Yanling Wang, Penn State University
#     updated:4/10/2019
# 
#     usage: ./driver.sh
# 

# Point values
MAX_BASIC=18
MAX_NOSIZE=10
MAX_CACHE=12
MAX_RANGE=10

# Various constants
HOME_DIR=`pwd`
PROXY_DIR="./.proxy"
NOPROXY_DIR="./.noproxy"
TIMEOUT=5
MAX_RAND=63000
PORT_START=1024
PORT_MAX=65000
MAX_PORT_TRIES=10

# List of text and binary files for the basic test
BASIC_LIST="home.html
            csapp.c
            tiny.c
	    sample.mp3
            godzilla.jpg
	    chopin.mp3
            shaun.mp4
            tiny
	    range.txt"
# List of files for nosize test
NOSIZE_LIST="csapp.c.nosize
             shaun.mp4.nosize
	     chopin.mp3.nosize
	     tiny.nosize
	     range.txt.nosize"
# List of text files for the cache test
CACHE_LIST="tiny.c
	    shaun.mp4
	    sample.mp3
	    godzilla.jpg
	    tiny"
# List of ranges for the range test
RANGE_LIST="0-19
	    35-
	    -25
	    50-105
	    -200
	    "

#####
# Helper functions
#

#
# download_proxy - download a file from the origin server via the proxy
# usage: download_proxy <testdir> <filename> <origin_url> <proxy_url>
#
function download_proxy {
    cd $1
    curl --max-time ${TIMEOUT} --silent --proxy $4 --output $2 $3
    (( $? == 28 )) && echo "Error: Fetch timed out after ${TIMEOUT} seconds"
    cd $HOME_DIR
}

#
# download_noproxy - download a file directly from the origin server
# usage: download_noproxy <testdir> <filename> <origin_url>
#
function download_noproxy {
    cd $1
    curl --max-time ${TIMEOUT} --silent --output $2 $3 
    (( $? == 28 )) && echo "Error: Fetch timed out after ${TIMEOUT} seconds"
    cd $HOME_DIR
}

#
# clear_dirs - Clear the download directories
#
function clear_dirs {
    rm -rf ${PROXY_DIR}/*
    rm -rf ${NOPROXY_DIR}/*
}

#
# wait_for_port_use - Spins until the TCP port number passed as an
#     argument is actually being used. Times out after 5 seconds.
#
function wait_for_port_use() {
    timeout_count="0"
    portsinuse=`netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
        | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
        | grep -E "[0-9]+" | uniq | tr "\n" " "`

    echo "${portsinuse}" | grep -wq "${1}"
    while [ "$?" != "0" ]
    do
        timeout_count=`expr ${timeout_count} + 1`
        if [ "${timeout_count}" == "${MAX_PORT_TRIES}" ]; then
            kill -ALRM $$
        fi

        sleep 1
        portsinuse=`netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
            | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
            | grep -E "[0-9]+" | uniq | tr "\n" " "`
        echo "${portsinuse}" | grep -wq "${1}"
    done
}


#
# free_port - returns an available unused TCP port 
#
function free_port {
    # Generate a random port in the range [PORT_START,
    # PORT_START+MAX_RAND]. This is needed to avoid collisions when many
    # students are running the driver on the same machine.
    port=$((( RANDOM % ${MAX_RAND}) + ${PORT_START}))

    while [ TRUE ] 
    do
        portsinuse=`netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
            | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
            | grep -E "[0-9]+" | uniq | tr "\n" " "`

        echo "${portsinuse}" | grep -wq "${port}"
        if [ "$?" == "0" ]; then
            if [ $port -eq ${PORT_MAX} ]
            then
                echo "-1"
                return
            fi
            port=`expr ${port} + 1`
        else
            echo "${port}"
            return
        fi
    done
}


#######
# Main 
#######

######
# Verify that we have all of the expected files with the right
# permissions
#

# Kill any stray proxies or tiny servers owned by this user
killall -q proxy tiny tinyrange nop-server.py 2> /dev/null

# Make sure we have a Tiny directory
if [ ! -d ./tiny ]
then 
    echo "Error: ./tiny directory not found."
    exit
fi

# If there is no Tiny executable, then try to build it
if [ ! -x ./tiny/tiny ]
then 
    echo "Building the tiny executable."
    (cd ./tiny; make)
    echo ""
fi

# Make sure we have all the Tiny files we need
if [ ! -x ./tiny/tiny ]
then 
    echo "Error: ./tiny/tiny not found or not an executable file."
    exit
fi
for file in ${BASIC_LIST}
do
    if [ ! -e ./tiny/${file} ]
    then
        echo "Error: ./tiny/${file} not found."
        exit
    fi
done

# Make sure we have an existing executable proxy
if [ ! -x ./proxy ]
then 
    echo "Error: ./proxy not found or not an executable file. Please rebuild your proxy and try again."
    exit
fi


# Create the test directories if needed
if [ ! -d ${PROXY_DIR} ]
then
    mkdir ${PROXY_DIR}
fi

if [ ! -d ${NOPROXY_DIR} ]
then
    mkdir ${NOPROXY_DIR}
fi

# Add a handler to generate a meaningful timeout message
trap 'echo "Timeout waiting for the server to grab the port reserved for it"; kill $$' ALRM


# Run the Tiny Web server
tiny_port=$(free_port)
echo "Starting tiny on ${tiny_port}"
cd ./tiny
./tiny ${tiny_port}   &> /dev/null  &
tiny_pid=$!
cd ${HOME_DIR}

# Wait for tiny to start in earnest
wait_for_port_use "${tiny_port}"

# Run the proxy
proxy_port=$(free_port)
echo "Starting proxy on ${proxy_port}"
./proxy ${proxy_port}  &> /dev/null &
proxy_pid=$!

# Wait for the proxy to start in earnest
wait_for_port_use "${proxy_port}"


#####
# Basic
#
echo "*** Basic ***"
# Now do the test by fetching some text and binary files directly from
# Tiny and via the proxy, and then comparing the results.
numRun=0
numSucceeded=0
for file in ${BASIC_LIST}
do
    numRun=`expr $numRun + 1`
    echo "${numRun}: ${file}"
    clear_dirs

    # Fetch using the proxy
    echo "   Fetching ./tiny/${file} into ${PROXY_DIR} using the proxy"
    download_proxy $PROXY_DIR ${file} "http://localhost:${tiny_port}/${file}" "http://localhost:${proxy_port}"

    # Fetch directly from Tiny
    echo "   Fetching ./tiny/${file} into ${NOPROXY_DIR} directly from Tiny"
    download_noproxy $NOPROXY_DIR ${file} "http://localhost:${tiny_port}/${file}"

    # Compare the two files
    echo "   Comparing the two files"
    diff -q ${PROXY_DIR}/${file} ${NOPROXY_DIR}/${file} &> /dev/null
    if [ $? -eq 0 ]; then
        numSucceeded=`expr ${numSucceeded} + 1`
        echo "   Success: Files are identical."
    else
        echo "   Failure: Files differ."
    fi
done
basicScore=`expr ${MAX_BASIC} \* ${numSucceeded} / ${numRun}`
echo "basicScore: $basicScore/${MAX_BASIC}"

echo "*** Nosize ***"
# Now do the test by fetching some text and binary files directly from
# Tiny and via the proxy, and then comparing the results.
numRun=0
numSucceeded=0
for file in ${NOSIZE_LIST}
do
    numRun=`expr $numRun + 1`
    echo "${numRun}: ${file}"
    clear_dirs

    # Fetch using the proxy
    echo "   Fetching ./tiny/${file} into ${PROXY_DIR} using the proxy"
    download_proxy $PROXY_DIR ${file} "http://localhost:${tiny_port}/${file}" "http://localhost:${proxy_port}"

    # Fetch directly from Tiny
    echo "   Fetching ./tiny/${file} into ${NOPROXY_DIR} directly from Tiny"
    download_noproxy $NOPROXY_DIR ${file} "http://localhost:${tiny_port}/${file}"

    # Compare the two files
    echo "   Comparing the two files"
    diff -q ${PROXY_DIR}/${file} ${NOPROXY_DIR}/${file} &> /dev/null
    if [ $? -eq 0 ]; then
        numSucceeded=`expr ${numSucceeded} + 1`
        echo "   Success: Files are identical."
    else
        echo "   Failure: Files differ."
    fi
done
nosizeScore=`expr ${MAX_NOSIZE} \* ${numSucceeded} / ${numRun}`
echo "nosizeScore: $nosizeScore/${MAX_NOSIZE}"

echo "Killing tiny and proxy"
kill $tiny_pid 2> /dev/null
wait $tiny_pid 2> /dev/null
kill $proxy_pid 2> /dev/null
wait $proxy_pid 2> /dev/null

#####
# Caching
#
echo ""
echo "*** Cache ***"


# Run the proxy
proxy_port=$(free_port)
echo "Starting proxy on port ${proxy_port}"
./proxy ${proxy_port} &> /dev/null &
proxy_pid=$!

# Wait for the proxy to start in earnest
wait_for_port_use "${proxy_port}"

# Fetch some files from tiny using the proxy
clear_dirs
numRun=0
numSucceeded=0
for file in ${CACHE_LIST}
do
    numRun=`expr $numRun + 1`
    echo "${numRun}: ${file}"
    # Run the Tiny Web server
    tiny_port=$(free_port)
    echo "Starting tiny on port ${tiny_port}"
    cd ./tiny
    ./tiny ${tiny_port} &> /dev/null &
    tiny_pid=$!
    cd ${HOME_DIR}

    # Wait for tiny to start in earnest
    wait_for_port_use "${tiny_port}"

    echo "Fetching ./tiny/${file} into ${PROXY_DIR} using the proxy"
    download_proxy $PROXY_DIR ${file} "http://localhost:${tiny_port}/${file}" "http://localhost:${proxy_port}"

    # Kill Tiny
    echo "Killing tiny"
    kill $tiny_pid 2> /dev/null
    wait $tiny_pid 2> /dev/null

    # Now try to fetch a cached copy of one of the fetched files.
    echo "Fetching a cached copy of ./tiny/${file} into ${NOPROXY_DIR}"
    download_proxy $NOPROXY_DIR ${file} "http://localhost:${tiny_port}/${file}" "http://localhost:${proxy_port}"

    # Compare the two files
    echo "   Comparing the two files"
    diff -q ./tiny/${file} ${NOPROXY_DIR}/${file} &> /dev/null
    if [ $? -eq 0 ]; then
        numSucceeded=`expr ${numSucceeded} + 1`
        echo "   Success: Files are identical."
    else
        echo "   Failure: Files differ."
    fi
done
# Kill the proxy
echo "Killing proxy"
kill $proxy_pid 2> /dev/null
wait $proxy_pid 2> /dev/null

cacheScore=`expr ${MAX_CACHE} \* ${numSucceeded} / ${numRun}`
echo "cacheScore: $cacheScore/${MAX_CACHE}"

#####
# Range 
#
echo ""
echo "*** Range ***"


# Run the proxy
proxy_port=$(free_port)
echo "Starting proxy on port ${proxy_port}"
./proxy ${proxy_port} &> /dev/null &
proxy_pid=$!

# Wait for the proxy to start in earnest
wait_for_port_use "${proxy_port}"

# Fetch some files from tiny using the proxy
clear_dirs
# Run the Tiny Web server
tiny_port=$(free_port)
echo "Starting tiny on port ${tiny_port}"
cd ./tiny
./tiny ${tiny_port} &> /dev/null &
tiny_pid=$!
cd ${HOME_DIR}

# Wait for tiny to start in earnest
wait_for_port_use "${tiny_port}"

echo "Fetching ./tiny/range.txt into ${PROXY_DIR} using the proxy"
download_proxy $PROXY_DIR range.txt "http://localhost:${tiny_port}/range.txt" "http://localhost:${proxy_port}"


numRun=0
numSucceeded=0
for range in ${RANGE_LIST}
do
    numRun=`expr $numRun + 1`
    echo "${numRun}: ${range}"

    # Now try to fetch a range of range.txt 
    echo "Fetching a range of ${range} in range.txt into ${PROXY_DIR}"
    download_proxy $PROXY_DIR range${numRun}.txt "http://localhost:${tiny_port}/range.txt" "http://localhost:${proxy_port} -r ${range}"

    # Compare the two files
    echo "   Comparing the two files"
    diff -q ./tiny/range${numRun}.txt ${PROXY_DIR}/range${numRun}.txt &> /dev/null
    if [ $? -eq 0 ]; then
        numSucceeded=`expr ${numSucceeded} + 1`
        echo "   Success: Files are identical."
    else
        echo "   Failure: Files differ."
    fi
done
# Kill Tiny
echo "Killing tiny"
kill $tiny_pid 2> /dev/null
wait $tiny_pid 2> /dev/null
# Kill the proxy
echo "Killing proxy"
kill $proxy_pid 2> /dev/null
wait $proxy_pid 2> /dev/null

rangeScore=`expr ${MAX_RANGE} \* ${numSucceeded} / ${numRun}`
echo "rangeScore: $rangeScore/${MAX_RANGE}"


# Emit the total score
totalScore=`expr ${basicScore} + ${nosizeScore} + ${cacheScore} + ${rangeScore}`
maxScore=`expr ${MAX_BASIC} + ${MAX_NOSIZE} + ${MAX_CACHE} + ${MAX_RANGE}`
echo ""
echo "totalScore: ${totalScore}/${maxScore}"
exit

