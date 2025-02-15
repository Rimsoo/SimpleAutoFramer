import threading
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('AppIndicator3', '0.1')
from gi.repository import Gtk, AppIndicator3, GLib
import cv2
import numpy as np
from pyvirtualcam import Camera
from PIL import Image, ImageTk
import threading
import tkinter as tk
import mediapipe as mp
import os

class AutoFramerGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("AutoFramer GUI")
        self.protocol("WM_DELETE_WINDOW", self.on_close)

        # Variables de configuration par défaut
        self.smoothing_var = tk.DoubleVar(value=0.1)
        self.confidence_var = tk.DoubleVar(value=0.3)
        self.model_selection_var = tk.IntVar(value=0)
        self.zoom_base_var = tk.DoubleVar(value=1.5)
        self.zoom_multiplier_var = tk.DoubleVar(value=0.0)
        # Résolution par défaut (non carrée) pour garder le ratio de la caméra
        self.width_var = tk.IntVar(value=1080)
        self.height_var = tk.IntVar(value=720)
        # Preview scale : multiplicateur de la résolution cible pour la zone de prévisualisation
        self.preview_scale_var = tk.DoubleVar(value=0.5)

        # Définir la résolution cible à partir des variables
        self.target_res = (self.width_var.get(), self.height_var.get())

        # Variables pour le lissage et le zoom (initialisées au centre et avec le zoom de base)
        self.last_center = np.array([0.5, 0.5])
        self.last_zoom = self.zoom_base_var.get()

        # Capture vidéo depuis la webcam
        self.cap = cv2.VideoCapture(1)

        # Initialisation de la caméra virtuelle (sortie à la résolution cible)
        self.virt_cam = Camera(width=self.target_res[0], height=self.target_res[1], fps=30)

        # Initialisation de MediaPipe pour la détection de visage
        self.mp_face_detection = mp.solutions.face_detection
        self.face_detection = self.mp_face_detection.FaceDetection(
            model_selection=self.model_selection_var.get(),
            min_detection_confidence=self.confidence_var.get()
        )

        # Construction de l'interface graphique en split view
        self.create_widgets()

        # Démarrer la boucle de mise à jour du flux vidéo
        self.update_frame()

    def create_widgets(self):
        # Créer un PanedWindow horizontal (split view)
        paned = tk.PanedWindow(self, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True)

        # Panneau de gauche : zone de prévisualisation avec fond noir et taille définie par le slider
        preview_width = int(self.target_res[0] * self.preview_scale_var.get())
        preview_height = int(self.target_res[1] * self.preview_scale_var.get())
        left_frame = tk.Frame(paned, bg="black", width=preview_width, height=preview_height)
        left_frame.pack_propagate(False)  # Empêche le redimensionnement automatique
        paned.add(left_frame, minsize=300)

        # Panneau de droite : contrôles
        right_frame = tk.Frame(paned)
        paned.add(right_frame, minsize=300)

        # Zone de prévisualisation
        self.preview_label = tk.Label(left_frame, bg="black")
        self.preview_label.pack(fill=tk.BOTH, expand=True)

        # Contrôles (dans right_frame)
        row = 0
        tk.Label(right_frame, text="Smoothing").grid(row=row, column=0, sticky="w", padx=5, pady=5)
        tk.Scale(right_frame, variable=self.smoothing_var, from_=0, to=1,
                resolution=0.01, orient=tk.HORIZONTAL, length=200).grid(row=row, column=1, padx=5, pady=5)
        row += 1
        tk.Label(right_frame, text="Min Detection Confidence").grid(row=row, column=0, sticky="w", padx=5, pady=5)
        tk.Scale(right_frame, variable=self.confidence_var, from_=0, to=1,
                resolution=0.01, orient=tk.HORIZONTAL, length=200).grid(row=row, column=1, padx=5, pady=5)
        row += 1
        tk.Label(right_frame, text="Model Selection").grid(row=row, column=0, sticky="w", padx=5, pady=5)
        rb_frame = tk.Frame(right_frame)
        rb_frame.grid(row=row, column=1, padx=5, pady=5)
        tk.Radiobutton(rb_frame, text="0", variable=self.model_selection_var, value=0).pack(side=tk.LEFT)
        tk.Radiobutton(rb_frame, text="1", variable=self.model_selection_var, value=1).pack(side=tk.LEFT)
        row += 1
        tk.Label(right_frame, text="Zoom Base").grid(row=row, column=0, sticky="w", padx=5, pady=5)
        tk.Scale(right_frame, variable=self.zoom_base_var, from_=1, to=3,
                resolution=0.1, orient=tk.HORIZONTAL, length=200).grid(row=row, column=1, padx=5, pady=5)
        row += 1
        tk.Label(right_frame, text="Zoom Multiplier").grid(row=row, column=0, sticky="w", padx=5, pady=5)
        tk.Scale(right_frame, variable=self.zoom_multiplier_var, from_=0, to=1,
                resolution=0.01, orient=tk.HORIZONTAL, length=200).grid(row=row, column=1, padx=5, pady=5)
        row += 1
        tk.Label(right_frame, text="Resolution Width").grid(row=row, column=0, sticky="w", padx=5, pady=5)
        tk.Entry(right_frame, textvariable=self.width_var, width=10).grid(row=row, column=1, sticky="w", padx=5, pady=5)
        row += 1
        tk.Label(right_frame, text="Resolution Height").grid(row=row, column=0, sticky="w", padx=5, pady=5)
        tk.Entry(right_frame, textvariable=self.height_var, width=10).grid(row=row, column=1, sticky="w", padx=5, pady=5)
        row += 1
        tk.Label(right_frame, text="Preview Scale").grid(row=row, column=0, sticky="w", padx=5, pady=5)
        tk.Scale(right_frame, variable=self.preview_scale_var, from_=0.1, to=1.0,
                resolution=0.1, orient=tk.HORIZONTAL, length=200, command=self.update_preview_size).grid(row=row, column=1, padx=5, pady=5)
        row += 1
        tk.Button(right_frame, text="Apply Settings", command=self.apply_settings).grid(row=row, column=0, columnspan=2, pady=10)

    def update_preview_size(self, event=None):
        # Met à jour la taille de la zone de prévisualisation en fonction du slider sans déformer l'image.
        new_width = int(self.target_res[0] * self.preview_scale_var.get())
        new_height = int(self.target_res[1] * self.preview_scale_var.get())
        # On redéfinit la taille du container de la preview
        self.preview_label.master.config(width=new_width, height=new_height)
        self.preview_label.master.pack_propagate(False)

    def show_window(self):
        self.after(0, self.deiconify)

    def quit_app(self):
        self.on_exit()

    def apply_settings(self):
        """Réapplique les paramètres en réinitialisant la caméra virtuelle et MediaPipe."""
        self.target_res = (self.width_var.get(), self.height_var.get())
        try:
            self.virt_cam.close()
        except Exception as e:
            print("Erreur lors de la fermeture de la caméra virtuelle :", e)
        self.virt_cam = Camera(width=self.target_res[0], height=self.target_res[1], fps=30)
        self.face_detection.close()
        self.face_detection = self.mp_face_detection.FaceDetection(
            model_selection=self.model_selection_var.get(),
            min_detection_confidence=self.confidence_var.get()
        )
        self.last_center = np.array([0.5, 0.5])
        if not self.last_zoom:
            self.last_zoom = self.zoom_base_var.get()
        self.update_preview_size()  # Mettre à jour la zone de prévisualisation si la résolution change
        print("Paramètres appliqués.")

    def update_frame(self):
        ret, frame = self.cap.read()
        if not ret:
            self.after(10, self.update_frame)
            return

        h, w = frame.shape[:2]
        frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = self.face_detection.process(frame_rgb)

        # Calculer un "base crop" qui respecte le ratio de la résolution cible
        target_ratio = self.target_res[0] / self.target_res[1]
        frame_ratio = w / h
        if frame_ratio > target_ratio:
            base_width = h * target_ratio
            base_height = h
        else:
            base_width = w
            base_height = w / target_ratio

        if results.detections:
            detection = results.detections[0]
            bboxC = detection.location_data.relative_bounding_box
            new_center = np.array([
                bboxC.xmin + bboxC.width / 2,
                bboxC.ymin + bboxC.height / 2
            ])
            smoothing = self.smoothing_var.get()
            self.last_center = smoothing * new_center + (1 - smoothing) * self.last_center
            new_zoom = self.zoom_base_var.get() + (detection.score[0] * self.zoom_multiplier_var.get())
            self.last_zoom = new_zoom

        # Calcul de la ROI en fonction du zoom et du "base crop"
        roi_width = int(base_width / self.last_zoom)
        roi_height = int(base_height / self.last_zoom)
        cx = int(self.last_center[0] * w)
        cy = int(self.last_center[1] * h)
        x = int(cx - roi_width / 2)
        y = int(cy - roi_height / 2)
        x = max(0, min(x, w - roi_width))
        y = max(0, min(y, h - roi_height))

        cropped = frame[y:y+roi_height, x:x+roi_width]
        cropped = cv2.resize(cropped, self.target_res)

        # Préparer la prévisualisation en conservant le ratio
        preview_size = (int(self.target_res[0] * self.preview_scale_var.get()),
                        int(self.target_res[1] * self.preview_scale_var.get()))
        # Convertir l'image en PIL
        im = Image.fromarray(cv2.cvtColor(cropped, cv2.COLOR_BGR2RGB))
        im_ratio = im.width / im.height
        target_preview_ratio = preview_size[0] / preview_size[1]
        if abs(im_ratio - target_preview_ratio) < 0.01:
            final_im = im.resize(preview_size, Image.LANCZOS)
        else:
            # Calculer la taille maximale qui rentre dans preview_size tout en gardant le ratio
            scale_factor = min(preview_size[0] / im.width, preview_size[1] / im.height)
            new_size = (int(im.width * scale_factor), int(im.height * scale_factor))
            resized_im = im.resize(new_size, Image.LANCZOS)
            # Créer une image de fond noire
            final_im = Image.new("RGB", preview_size, (0, 0, 0))
            offset = ((preview_size[0] - new_size[0]) // 2, (preview_size[1] - new_size[1]) // 2)
            final_im.paste(resized_im, offset)

        imgtk = ImageTk.PhotoImage(image=final_im)
        self.preview_label.imgtk = imgtk  # Conserver la référence
        self.preview_label.config(image=imgtk)

        # Envoyer la frame traitée à la caméra virtuelle
        self.virt_cam.send(cv2.cvtColor(cropped, cv2.COLOR_BGR2RGB))
        self.virt_cam.sleep_until_next_frame()

        self.after(1, self.update_frame)

    def on_close(self):
        """Masquer la fenêtre sans arrêter l'application."""
        self.withdraw()

    def on_exit(self):
        """Libérer les ressources et fermer l'application définitivement."""
        if self.cap.isOpened():
            self.cap.release()
        self.face_detection.close()
        self.virt_cam.close()
        self.destroy()
        Gtk.main_quit() 


def indicator_thread():

    ICON_PATH = os.path.abspath("icon.png")
    """ Gère l'icône dans la barre système avec AppIndicator """
    indicator = AppIndicator3.Indicator.new(
        "AutoFramer", ICON_PATH, AppIndicator3.IndicatorCategory.APPLICATION_STATUS)
    indicator.set_status(AppIndicator3.IndicatorStatus.ACTIVE)

    # Création du menu
    menu = Gtk.Menu()

    # Élément "Show" qui réaffiche la fenêtre Tkinter
    item_show = Gtk.MenuItem(label="Show")
    item_show.connect("activate", lambda widget: GLib.idle_add(app.deiconify))  # Utilisation de GLib.idle_add
    menu.append(item_show)

    # Élément "Quit" qui ferme l'application
    item_quit = Gtk.MenuItem(label="Quit")
    def on_quit(widget):
        GLib.idle_add(app.on_exit)  # Quitter Tkinter proprement

    item_quit.connect("activate", on_quit)
    menu.append(item_quit)

    menu.show_all()
    indicator.set_menu(menu)

    Gtk.main()  # Démarrer GTK

if __name__ == "__main__":
    # Création de l'interface Tkinter
    app = AutoFramerGUI()

    # Lancer AppIndicator dans un thread séparé
    t = threading.Thread(target=indicator_thread, daemon=True)
    t.start()

    app.mainloop()  # Démarrer Tkinter
