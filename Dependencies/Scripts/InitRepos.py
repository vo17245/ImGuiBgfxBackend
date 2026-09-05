import subprocess
import sys
import os
def clone_repo(url:str,work_dir:str,commit:str):
    cwd=os.getcwd()
    repo_name=url.split("/")[-1][:-4]
    os.chdir(work_dir)
    if not os.path.exists(repo_name):
        subprocess.run([
            "git",
            "clone",
            url
        ])
    os.chdir(f"./{repo_name}")
    subprocess.run([
        "git",
        "checkout",
        commit
    ])
    os.chdir(cwd)
def apply_patches(path:str,patches:list[str]):
    cwd=os.getcwd()
    pathes_dir=f"{cwd}/Patches"
    os.chdir(path)
    for patch in patches:
        subprocess.run([
            "git",
            "apply",
            f"{pathes_dir}/{patch}"
        ])

    os.chdir(cwd)
import json
import pathlib
with open("./Dependencies.json","r",encoding="utf-8") as f:
    deps=json.loads(f.read())

for dep in deps:
    print(dep)
    url=dep["url"]
    path=f"Repos/{dep["path"]}"
    repo_parent_path=str(pathlib.Path(path).parent)
    if not os.path.exists(repo_parent_path):
        os.makedirs(repo_parent_path)
    commit=dep["commit"]
    clone_repo(url,repo_parent_path,commit)
    if "patches" not in dep:
        continue
    patches=dep["patches"]
    apply_patches(path,patches)
    
