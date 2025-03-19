docker container ls -a | grep 'muppet-docker' &> /dev/null
if [ $? == 0 ]; then
    docker start muppet-docker
    docker exec -it --user root muppet-docker bash
else
    docker run -it -v "$PWD":/root/muppet-docker --name muppet-docker ucdavisplse/muppet-docker:latest
fi
