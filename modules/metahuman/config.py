def can_build(env, platform):
    env.module_add_dependencies("metahuman", ["fbx"], True)
    return not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "EditorSceneFormatImporterMetaHuman",
        "MetaHumanDNA",
        "MetaHumanHairSimulation",
        "MetaHumanHairSimulator",
        "MetaHumanRig",
    ]


def get_doc_path():
    return "doc_classes"
