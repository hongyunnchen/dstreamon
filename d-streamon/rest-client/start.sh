#! /bin/bash

CONFIG=$1
echo $CONFIG
echo '{"name":"'$CONFIG'"}'
curl -i -X POST -H "Content-Type:application/json" http://localhost:8080/api/commands/start -d '{"name":"'$CONFIG'"}'

