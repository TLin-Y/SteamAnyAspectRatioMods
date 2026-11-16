extends Node

const BASE_SIZE := Vector2(1920, 1080)
const MAIN_SCENE := "res://main.tscn"
const EXPAND_SCENES := [
	"res://main.tscn",
	"res://ui/menus/run/end_run.tscn",
	"res://ui/menus/run/coop_end_run.tscn",
]
const NARROW_EXPAND_SCENES := [
	"res://ui/menus/shop/shop.tscn",
	"res://ui/menus/shop/coop_shop.tscn",
]

var _scene_file := ""
var _expand := false


func _ready() -> void:
	pause_mode = Node.PAUSE_MODE_PROCESS
	get_tree().connect("node_added", self, "_on_node_added")
	get_tree().root.connect("size_changed", self, "_on_size_changed")
	var scene := get_tree().current_scene
	if scene != null:
		_scene_file = scene.filename
	_apply()


func _on_node_added(node: Node) -> void:
	if node.get_parent() != get_tree().root or node.filename == "":
		return
	if node.filename in EXPAND_SCENES or node.filename in NARROW_EXPAND_SCENES or node.filename.begins_with("res://ui/menus/"):
		_scene_file = node.filename
		_apply()
		if node.filename == MAIN_SCENE:
			node.connect("ready", self, "_fit_main", [node], CONNECT_ONESHOT | CONNECT_DEFERRED)


func _wants_expand() -> bool:
	if _scene_file in EXPAND_SCENES:
		return true
	if _scene_file in NARROW_EXPAND_SCENES:
		var window := OS.window_size
		return window.y > 0 and window.x / window.y <= BASE_SIZE.x / BASE_SIZE.y + 0.01
	return false


func _apply() -> void:
	var expand := _wants_expand()
	if expand != _expand:
		_expand = expand
		var aspect := SceneTree.STRETCH_ASPECT_EXPAND if expand else SceneTree.STRETCH_ASPECT_KEEP
		get_tree().set_screen_stretch(SceneTree.STRETCH_MODE_2D, aspect, BASE_SIZE)
	_update_size()


func _update_size() -> void:
	var size := get_tree().root.get_visible_rect().size if _expand else BASE_SIZE
	Utils.project_width = int(round(size.x))
	Utils.project_height = int(round(size.y))


func _on_size_changed() -> void:
	call_deferred("_on_resized")


func _on_resized() -> void:
	_apply()
	var scene := get_tree().current_scene
	if scene != null and scene.filename == MAIN_SCENE and scene.is_inside_tree():
		_fit_main(scene)


func _fit_main(main: Node) -> void:
	if not is_instance_valid(main):
		return
	var size := Vector2(Utils.project_width, Utils.project_height)
	var bg := main.get_node_or_null("CanvasLayer/Background") as Control
	if bg != null:
		bg.anchor_right = 0.0
		bg.anchor_bottom = 0.0
		bg.rect_position = Vector2(size.x, 0.0)
		bg.rect_size = Vector2(size.y, size.x + 120.0)
	var fog := main.get_node_or_null("fog_viewport") as Viewport
	if fog != null and not fog.is_queued_for_deletion() and fog.size != Vector2(2, 2):
		fog.size = size
