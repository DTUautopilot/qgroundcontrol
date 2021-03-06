FIND_PATH(FLITE_INCLUDE_DIR flite/flite.h)

FIND_LIBRARY(FLITE_MAIN_LIB NAMES flite)
FIND_LIBRARY(FLITE_CMULEX_LIB NAMES flite_cmulex)
FIND_LIBRARY(FLITE_CMU_US_KAL_LIB NAMES flite_cmu_us_kal)
FIND_LIBRARY(FLITE_US_ENGLISH_LIB NAMES flite_usenglish)

SET(FLITE_LIBRARIES
	${FLITE_MAIN_LIB}
	${FLITE_CMULEX_LIB}
	${FLITE_CMU_US_KAL_LIB}
	${FLITE_US_ENGLISH_LIB}
	)

IF(FLITE_INCLUDE_DIR AND FLITE_LIBRARIES)
	SET(FLITE_FOUND TRUE)
ENDIF(FLITE_INCLUDE_DIR AND FLITE_LIBRARIES)

IF(FLITE_FOUND)
	IF (NOT Flite_FIND_QUIETLY)
		MESSAGE(STATUS "Found flite includes:	${FLITE_INCLUDE_DIR}/flite/flite.h")
		MESSAGE(STATUS "Found flite library: ${FLITE_LIBRARIES}")
	ENDIF (NOT Flite_FIND_QUIETLY)
ELSE(FLITE_FOUND)
	IF (Flite_FIND_REQUIRED)
		MESSAGE(FATAL_ERROR "Could NOT find flite development files")
	ENDIF (Flite_FIND_REQUIRED)
ENDIF(FLITE_FOUND)
