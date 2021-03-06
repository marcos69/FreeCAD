# Try to find Coin3D
# Once done this will define
#
# COIN3D_FOUND        - system has Coin3D - Open Inventor
# COIN3D_INCLUDE_DIR  - where the Inventor include directory can be found
# COIN3D_LIBRARY      - Link this to use Coin3D
#
 

IF (WIN32)
  IF (CYGWIN)

    FIND_PATH(COIN3D_INCLUDE_DIR Inventor/So.h
      /usr/include
      /usr/local/include
    )

    FIND_LIBRARY(COIN3D_LIBRARY Coin
      /usr/lib
      /usr/local/lib
    )

  ELSE (CYGWIN)

    FIND_PATH(COIN3D_INCLUDE_DIR Inventor/So.h
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\SIM\\Coin3D\\2;Installation Path]/include"
    )

    FIND_LIBRARY(COIN3D_LIBRARY_DEBUG coin2d
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\SIM\\Coin3D\\2;Installation Path]/lib"
    )

    FIND_LIBRARY(COIN3D_LIBRARY_RELEASE coin2
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\SIM\\Coin3D\\2;Installation Path]/lib"
    )

    IF (COIN3D_LIBRARY_DEBUG AND COIN3D_LIBRARY_RELEASE)
      SET(COIN3D_LIBRARY optimized ${COIN3D_LIBRARY_RELEASE}
                         debug ${COIN3D_LIBRARY_DEBUG})
    ELSE (COIN3D_LIBRARY_DEBUG AND COIN3D_LIBRARY_RELEASE)
      IF (COIN3D_LIBRARY_DEBUG)
        SET (COIN3D_LIBRARY ${COIN3D_LIBRARY_DEBUG})
      ENDIF (COIN3D_LIBRARY_DEBUG)
      IF (COIN3D_LIBRARY_RELEASE)
        SET (COIN3D_LIBRARY ${COIN3D_LIBRARY_RELEASE})
      ENDIF (COIN3D_LIBRARY_RELEASE)
    ENDIF (COIN3D_LIBRARY_DEBUG AND COIN3D_LIBRARY_RELEASE)

    IF (COIN3D_LIBRARY)
    #  ADD_DEFINITIONS ( -DCOIN_NOT_DLL )
    #ELSE (COIN3D_LIBRARY)
    #  SET (COIN3D_LIBRARY coin2d CACHE STRING "Coin3D Library (Debug) - Open Inventor API")
    ENDIF (COIN3D_LIBRARY)

  ENDIF (CYGWIN)

ELSE (WIN32)
  IF(APPLE)
    FIND_PATH(COIN3D_INCLUDE_DIR Inventor/So.h
     /Library/Frameworks/Inventor.framework/Headers 
     /usr/local/include
     /usr/include
    )
    FIND_LIBRARY(COIN3D_LIBRARY Coin
      /Library/Frameworks/Inventor.framework/Libraries
      /usr/lib
      /usr/local/lib
    )   
    SET(COIN3D_LIBRARY "-framework Coin3d" CACHE STRING "Coin3D library for OSX")
   ELSE(APPLE)

  FIND_PATH(COIN3D_INCLUDE_DIR Inventor/So.h
    /usr/include
    /usr/local/include
  )

  FIND_LIBRARY(COIN3D_LIBRARY Coin
    /usr/lib
    /usr/local/lib
  )   
  ENDIF(APPLE)

ENDIF (WIN32)


SET( COIN3D_FOUND "NO" )
IF(COIN3D_LIBRARY)
  SET( COIN3D_FOUND "YES" )
ENDIF(COIN3D_LIBRARY)

