"""Content failures that name the Tiled file, room, layer and object an author has to change."""


class ContentError(ValueError):
    # a ValueError subclass, so every existing caller that handles converter failures still does.
    def __init__(self, message, file=None, room=None, layer=None, object_id=None, object_name=None, fix=None):
        self.message = message
        self.file = str(file).replace('\\', '/') if file else None
        self.room = room
        self.layer = layer
        self.object_id = object_id
        self.object_name = object_name
        self.fix = fix
        super().__init__(str(self))

    @classmethod
    def at(cls, message, recipe=None, obj=None, fix=None):
        """Locate a failure from a room recipe (map, title) and a map_objects() entry."""
        recipe = recipe or {}
        obj = obj or {}
        return cls(message, file=recipe.get('map'), room=recipe.get('title'), layer=obj.get('group'),
                   object_id=obj.get('id'), object_name=obj.get('name'), fix=fix)

    def location(self):
        parts = []
        if self.object_id is not None:
            parts.append(f'object {self.object_id}' + (f' "{self.object_name}"' if self.object_name else ''))
        details = [f'layer {self.layer}'] if self.layer else []
        if self.room:
            details.append(f'room {self.room}')
        if details:
            parts.append('(' + ', '.join(details) + ')' if parts else ', '.join(details))
        where = ' '.join(parts)
        if self.file:
            return f'{self.file}: {where}' if where else self.file
        return where

    def __str__(self):
        where = self.location()
        text = f'{where}: {self.message}' if where else self.message
        return f'{text}\n  fix: {self.fix}' if self.fix else text
