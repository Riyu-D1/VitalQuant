/* list.h — host-sim kernel list structures.
 * Real doubly-linked circular list with xListEnd sentinel, as upstream.
 * Mostly unused by the firmware but kept functional. */
#ifndef INC_LIST_H
#define INC_LIST_H

#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xLIST;
struct xLIST_ITEM
{
    TickType_t           xItemValue;
    struct xLIST_ITEM   *pxNext;
    struct xLIST_ITEM   *pxPrevious;
    void                *pvOwner;
    struct xLIST        *pxContainer;
};
typedef struct xLIST_ITEM ListItem_t;

struct xMINI_LIST_ITEM
{
    TickType_t           xItemValue;
    struct xLIST_ITEM   *pxNext;
    struct xLIST_ITEM   *pxPrevious;
};
typedef struct xMINI_LIST_ITEM MiniListItem_t;

struct xLIST
{
    UBaseType_t          uxNumberOfItems;
    ListItem_t          *pxIndex;
    MiniListItem_t       xListEnd;
};
typedef struct xLIST List_t;

#define listGET_OWNER_OF_HEAD_ENTRY(pxList) \
    (((pxList)->xListEnd.pxNext)->pvOwner)
#define listGET_LIST_ITEM_OWNER(pxItem)   ((pxItem)->pvOwner)
#define listGET_LIST_ITEM_VALUE(pxItem)   ((pxItem)->xItemValue)
#define listSET_LIST_ITEM_VALUE(pxItem, v) ((pxItem)->xItemValue = (v))
#define listLIST_ITEM_CONTAINER(pxItem)   ((pxItem)->pxContainer)
#define listCURRENT_LIST_LENGTH(pxList)   ((pxList)->uxNumberOfItems)
#define listLIST_IS_EMPTY(pxList) \
    ((BaseType_t)((pxList)->uxNumberOfItems == (UBaseType_t)0))
#define listLIST_IS_INITIALISED(pxList)   ((pxList) != NULL)
#define listGET_END_MARKER(pxList) \
    ((ListItem_t *)(&((pxList)->xListEnd)))
#define listGET_NEXT(pxItem)              ((pxItem)->pxNext)
#define listGET_HEAD_ENTRY(pxList)        ((pxList)->xListEnd.pxNext)

void        vListInitialise(List_t *pxList);
void        vListInitialiseItem(ListItem_t *pxItem);
void        vListInsert(List_t *pxList, ListItem_t *pxNewListItem);
void        vListInsertEnd(List_t *pxList, ListItem_t *pxNewListItem);
UBaseType_t uxListRemove(ListItem_t *pxItemToRemove);

#ifdef __cplusplus
}
#endif

#endif /* INC_LIST_H */
