# cmake -DSOURCE=<file> -DDESTINATION=<file> -P CopyIfExists.cmake
# Copies an optional build input when it exists and differs; a missing one is not an error.
if(EXISTS "${SOURCE}")
    file(COPY_FILE "${SOURCE}" "${DESTINATION}" ONLY_IF_DIFFERENT)
endif()
