#!/bin/sh

set -e

ARCH=amd64

DOCKERHUB_PAGESIZE=100

handle_image () {
	echo "found image: $1 $2"
	[ -f "$1/$2.filter" ] && return
	IMAGE=$(mktemp)
	skopeo copy "docker://$1@$2" "oci-archive:$IMAGE"
	# TODO debug double free on v2c-mkfindlayerfilter with some images
	v2c-mkfilter "$IMAGE" "$1/$2.filter" || true
	rm "$IMAGE"
}

dockerhub () {
	mkdir -p "docker.io/$1"
	PAGE=1
	DATA=$(curl "https://hub.docker.com/v2/repositories/$1/tags/?page_size=$DOCKERHUB_PAGESIZE&page=$PAGE")
	while [ $(echo "$DATA" | jq '.results|length') -eq $DOCKERHUB_PAGESIZE ]; do
		for i in $(echo "$DATA" | jq -r ".results[]|select(.tag_status == \"active\").images[]|select(.architecture == \"$ARCH\").digest"); do
			handle_image "docker.io/$1" "$i"
		done
		PAGE=$((PAGE + 1))
		DATA=$(curl "https://hub.docker.com/v2/repositories/$1/tags/?page_size=$DOCKERHUB_PAGESIZE&page=$PAGE")
	done
	for i in $(echo "$DATA" | jq -r ".results[]|select(.tag_status == \"active\").images[]|select(.architecture == \"$ARCH\").digest"); do
		handle_image "docker.io/$1" "$i"
	done
}

# TODO: follow link header, check architecture
oci_dist_with_docker_digest () {
	DATA=$(curl "https://$1/v2/$2/tags/list")
	for i in $(echo "$DATA" | jq -r ".tags[]"); do
		DIGEST=$(curl -I "https://$1/v2/$2/manifests/$i" | grep '^docker-content-digest:' | cut -f 2 -d ' ')
		handle_image "$1/$2" "$DIGEST"
	done
}

#oci_dist_with_docker_digest quay.io prometheus/prometheus

# dockerhub official images
for i in alpine debian ubuntu centos fedora archlinux; do
	dockerhub "library/$i"
done

# opensuse official images
for i in leap tumbleweed; do
	dockerhub "opensuse/$i"
done
