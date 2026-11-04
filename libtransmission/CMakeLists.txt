<<<<<<< SEARCH
    endif()
endmacro()

tr_allow_compile_if(
=======
    endif()
endmacro()

# Macro to conditionally add source files based on boolean expressions
# Usage: tr_allow_compile_if(
#   [=[[CONDITION1]]=] src1 src2 ...
#   [=[[CONDITION2]]=] src3 ...
# )
# Conditions are detected by the [=[[...]]=] bracket syntax.
macro(tr_allow_compile_if)
    # Collect all arguments
    set(_args ${ARGN})
    set(_current_condition "")
    set(_current_sources "")
    
    foreach(_arg IN LISTS _args)
        # Check if this argument is a condition (wrapped in [=[[...]]=])
        if(_arg MATCHES "^\\[=\\[\\[.*\\]\\]=\\]$")
            # If we have a previous condition with sources, process it
            if(_current_condition AND _current_sources)
                if(${_current_condition})
                    target_sources(${TR_NAME} PRIVATE ${_current_sources})
                endif()
            endif()
            # Start new condition - extract the inner condition from [=[[...]]=]
            string(REGEX MATCH "^\\[=\\[\\[(.*)\\]\\]=\\]$" _match "${_arg}")
            if(CMAKE_MATCH_1)
                set(_current_condition "${CMAKE_MATCH_1}")
            else()
                message(FATAL_ERROR "tr_allow_compile_if: Failed to parse condition from: ${_arg}")
            endif()
            set(_current_sources "")
        else()
            # This is a source file for the current condition
            if(_current_condition)
                list(APPEND _current_sources "${_arg}")
            else()
                message(FATAL_ERROR "tr_allow_compile_if: Source file '${_arg}' appears before any condition")
            endif()
        endif()
    endforeach()
    
    # Process the last condition
    if(_current_condition AND _current_sources)
        if(${_current_condition})
            target_sources(${TR_NAME} PRIVATE ${_current_sources})
        endif()
    endif()
endmacro()

tr_allow_compile_if(
>>>>>>> REPLACE
