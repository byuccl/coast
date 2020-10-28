#ifndef __MxM_H
#define __MxM_H

/********************************* Prototypes *********************************/
void vStartMMTasks( void );
void vEndMMTasks( void );

uint32_t xAreMMTasksStillRunning( void );
void vMMTaskCountClear( void );
void vMMTaskCountPrint( void );


#endif  /* __MxM_H */
