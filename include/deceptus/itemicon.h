#ifndef ITEM_ICON_H
#define ITEM_ICON_H

/* the inventory's item pictures, in the order of the adventure's item numbers 1 .. ITEM_ICON_COUNT
 * (item 1 is image 0), each centred in an ITEM_ICON_CELL square. while the inventory is open the
 * selected item's picture is the only sprite, ITEM_ICON_TOP below the top of the screen and centred
 * on the 8px grid so every backend's menu can leave room for it. */
#define ITEM_ICON_COUNT 7
#define ITEM_ICON_CELL 40
#define ITEM_ICON_TOP 48

#endif
