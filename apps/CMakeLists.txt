# Toomey May 2026
# Applications using the ldm library
#

# ==============================================================================
# Executable Definition (ldmd)
# ==============================================================================
addLDMBinary(ldmd
    SOURCES ldmd/ldmd.cpp
)

# ==============================================================================
# Executable Definition (pqcreate)
# ==============================================================================
addLDMBinary(pqcreate
    SOURCES pqcreate.cpp
)

# ==============================================================================
# Executable Definition (pqinsert)
# ==============================================================================
# The product queue ingestion utility
addLDMBinary(pqinsert
    SOURCES pqinsert.cpp
)

# ==============================================================================
# Executable Definition (pqmon)
# ==============================================================================
# The queue health monitoring utility
addLDMBinary(pqmon    
    SOURCES pqmon.cpp
)

# ==============================================================================
# Executable Definition (feedme)
# ==============================================================================
addLDMBinary(feedme    
    SOURCES feedme.cpp
)

# ==============================================================================
# Executable Definition (uldbutil)
# ==============================================================================
addLDMBinary(uldbutil    
    SOURCES uldbutil.cpp
)

# ==============================================================================
# Executable Definition (ldmping)
# ==============================================================================
addLDMBinary(ldmping   
    SOURCES ldmping.cpp
)

# ==============================================================================
# Executable Definition (notifyme)
# ==============================================================================
addLDMBinary(notifyme  
    SOURCES notifyme.cpp
)

# ==============================================================================
# Executable Definition (pqcheck)
# ==============================================================================
addLDMBinary(pqcheck   
    SOURCES pqcheck.cpp
)

# ==============================================================================
# Executable Definition (pqutil)
# ==============================================================================
addLDMBinary(pqutil   
    SOURCES pqutil.cpp
)

# ==============================================================================
# Executable Definition (pqcat)
# ==============================================================================
addLDMBinary(pqcat
    SOURCES pqcat.cpp
)

# ==============================================================================
# Executable Definition (regutil)
# ==============================================================================
addLDMBinary(regutil
    SOURCES regutil.cpp
)

# ==============================================================================
# Executable Definition (pqact)
# The downstream action/file-dispatching engine 
# ==============================================================================
addLDMBinary(pqact
    SOURCES 
        pqact/pqact.cpp
        pqact/ActionFactory.cpp
        pqact/FileCache.cpp
        pqact/FileOpsUtil.cpp
        pqact/UnioEntry.cpp
        pqact/PipeEntry.cpp
        pqact/FileAction.cpp
        pqact/PipeAction.cpp
        pqact/ExecAction.cpp
        pqact/PipeBuffer.cpp
        pqact/StateFile.cpp
        pqact/PqactConfig.cpp
        pqact/PqactParser.cpp
)
