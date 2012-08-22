# Find the spnav library and header.
#
# Sets the usual variables expected for find_package scripts:
#
# COSPNAV_INCLUDE_DIR - header location
# COSPNAV_LIBRARIES - library to link against
# CODPNAV_FOUND - true if pugixml was found.

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
#	set(BuildPlatform macosx)

#IF(UNIX)

    FIND_PATH(COSPNAV_INCLUDE_DIR 3DconnexionClient/ConnexionClientAPI.h
    PATHS
    /Library/Frameworks/3DconnexionClient.framework/Headers
    )

#    FIND_LIBRARY(COSPNAV_LIBRARY 3DconnexionClient
#    NAMES 3DconnexionClient
#    PATHS
#    /Library/Frameworks/3DconnexionClient.framework/3DconnexionClient
#    )

# Support the REQUIRED and QUIET arguments, and set COSPNAV_FOUND if found.
#include(FindPackageHandleStandardArgs)
#FIND_PACKAGE_HANDLE_STANDARD_ARGS(3DconnexionClient DEFAULT_MSG COSPNAV_LIBRARY
#                                  COSPNAV_INCLUDE_DIR)

SET(COSPNAV_FOUND TRUE)

SET(COSPNAV_LIBRARY "-weak_framework 3DconnexionClient -framework Carbon" CACHE STRING "3Dconnexion library for OSX")
SET(COSPNAV_LIBRARIES ${COSPNAV_LIBRARY})



mark_as_advanced(COSPANV_LIBRARY COSPNAV_INCLUDE_DIR)

#ENDIF(UNIX)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
