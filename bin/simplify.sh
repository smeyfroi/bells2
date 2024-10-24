#!/bin/bash

WORK_DIR="${PWD}/vpype-workdir"

#docker run \
#  -v $WORK_DIR:/usr/vpype/hostdir \
#  -it \
#  vpype \
#  sh -c '
#  vpype --help
#'

#docker run \
#  -v $WORK_DIR:/usr/vpype/hostdir \
#  -it \
#  vpype \
#  sh -c '
#  vpype \
#  forfile "hostdir/*[0-9].svg" \
#    read --attr stroke %_path% \
#    eval "w,h=prop.vp_page_size" \
#    deduplicate --progress-bar -t 0.1mm \
#    filter --min-length 0.08mm \
#    layout --fit-to-margins 0.0cm 15x15cm \
#    write "%prop.vp_source.with_stem(prop.vp_source.stem + \".processed\")%" \
#  end
#'

#    splitall linemerge --tolerance 0.5mm \

docker run \
  -v $WORK_DIR:/usr/vpype/hostdir \
  -it \
  vpype \
  sh -c '
  parallel \
    --plus \
    --tag --linebuffer \
      vpype \
        read --attr stroke {} \
        deduplicate --progress-bar -t 0.1mm \
        filter --min-length 0.05mm \
        layout --fit-to-margins 0.0cm 15x15cm \
        write {/.svg/_processed.svg} \
    ::: hostdir/*[0-9].svg
'
#    --dry-run \

# min-length 0.08 for bells
