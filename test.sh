RAW_DOCKER_JSON=$(curl -s "https://hub.docker.com/v2/repositories/rostooling/setup-ros-docker/tags?page_size=1000")
ACTIVE_ROS_DISTROS=$( echo $RAW_DOCKER_JSON | jq -r '.results[] | select(.tag_status=="active") | select(.name | contains("ros-base-latest")) | .name' | sort -u)

echo $ACTIVE_ROS_DISTROS

LIST_OF_DISTROS=""
DICT_OF_DISTRO_INFO=""
for distro_str in $ACTIVE_ROS_DISTROS; do
  IFS="-" read -ra SPLIT_STR <<< $distro_str
  LIST_OF_DISTROS+="\"${SPLIT_STR[3]}\" "
  DICT_OF_DISTRO_INFO+="{docker_image:\"${distro_str}\",ros_distro:\"${SPLIT_STR[3]}\"} "
done
DISTRO_STR=$(echo ${LIST_OF_DISTROS[@]} | sed -e 's|^ *|[|' -e 's| *$|]|' -e 's|  *|, |g')
MATRIX_STR=$(echo ${DICT_OF_DISTRO_INFO[@]} | sed -e 's|^ *|[|' -e 's| *$|]|' -e 's|  *|, |g')

echo $DISTRO_STR
echo $MATRIX_STR

