docker rm -f mongo 2>/dev/null

docker run -d \
  -p 27017:27017 \
  --name mongo \
  -v /mnt/big/srv/mongrel:/data/db \
  mongo:latest