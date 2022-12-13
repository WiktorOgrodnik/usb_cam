RAW_DOCKER_JSON=$(curl -s "https://hub.docker.com/v2/repositories/rostooling/setup-ros-docker/tags?page_size=1000")
ACTIVE_ROS_DISTROS=$( echo $RAW_DOCKER_JSON | jq -r '.results[] | select(.tag_status=="active") | select(.name | contains("ros-base-latest")) | .name' | sort -u)
echo $ACTIVE_ROS_DISTROS          
LIST_OF_DISTROS=""
DICT_OF_DISTRO_INFO=""
# TODO(flynnev): figure out automatic way to filter these instead
DEPRECATED_DISTROS=("dashing" "eloquent" "melodic")
for distro_str in $ACTIVE_ROS_DISTROS; do
  IFS="-" read -ra SPLIT_STR <<< $distro_str
  DISTRO="${SPLIT_STR[3]}"
  skip_distro=1
  echo $DISTRO
  for dep_distro in "${DEPRECATED_DISTROS[@]}"; do
    echo $dep_distro
    if [[ "$DISTRO" == "${dep_distro}" ]]; then
      skip_distro=0
      break
    fi
  done
  echo $skip_distro
  if [[ ${skip_distro} == 0 ]]; then
    continue
  fi
  LIST_OF_DISTROS+="\"${DISTRO}\" "
  DICT_OF_DISTRO_INFO+="{docker_image:\"${distro_str}\",ros_distro:\"${DISTRO}\"} "
done
DISTRO_STR=$(echo ${LIST_OF_DISTROS[@]} | sed -e 's|^ *|[|' -e 's| *$|]|' -e 's|  *|, |g')
MATRIX_STR=$(echo ${DICT_OF_DISTRO_INFO[@]} | sed -e 's|^ *|[|' -e 's| *$|]|' -e 's|  *|, |g')
echo "DISTRO_STR: ${DISTRO_STR}"
echo "MATRIX_STR: ${MATRIX_STR}"
echo "series=$DISTRO_STR" >> $GITHUB_OUTPUT
echo "matrix=$MATRIX_STR" >> $GITHUB_OUTPUT