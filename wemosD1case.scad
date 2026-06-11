// --- sélecteur d'affichage pour l'impression ---
// 1 = base du boîtier seul, 2 = couvercle seul, 3 = les deux pièces
choix_piece = 3; 

// --- paramètres du boîtier (adaptés pour wemos d1 + clavier long) ---
longueur_interne = 60;   // maintenu à 60 pour accueillir le clavier de 57mm
largeur_interne  = 26;   // 25.5mm + jeu
hauteur_interne  = 8.5;  // 7mm + jeu pour les soudures/composants sous la carte
epaisseur_paroi  = 2;
jeu              = 0.5; 
rayon_boitier    = 3;   

// --- paramètres de la barre de stop (butée pour bloquer le wemos d1) ---
wemos_longueur   = 34.5;
stop_largeur     = 3;   // épaisseur de la barre de stop
stop_hauteur     = 4;   // hauteur de la barre pour bloquer le pcb sans toucher les puces

// --- paramètres des bandes latérales internes (bords gauche et droit) ---
bande_largeur   = 2; 
bande_epaisseur = 2; 

// --- paramètres de la découpe du clavier ---
clavier_longueur = 0;
clavier_largeur  = 00;
clavier_epaisseur= 0; 

// --- calculs des dimensions externes ---
longueur_externe = longueur_interne + (2 * epaisseur_paroi) + jeu;
largeur_externe  = largeur_interne + (2 * epaisseur_paroi) + jeu;
hauteur_boite    = hauteur_interne + epaisseur_paroi;

$fn = 64; 

// --- logique d'affichage ---
if (choix_piece == 1) {
    base_boitier();
}
else if (choix_piece == 2) {
    translate([0, 0, epaisseur_paroi]) 
        rotate([0, 180, 0]) 
        couvercle();
}
else if (choix_piece == 3) {
    translate([0, 0, 0]) base_boitier();
    
    translate([longueur_externe, largeur_externe + 10, epaisseur_paroi]) 
        rotate([0, 180, 0]) 
        couvercle();
}

// --- module d'aide : cube avec coins verticaux arrondis ---
module cube_arrondi(x, y, z, r) {
    translate([r, r, 0])
    minkowski() {
        cube([x - 2*r, y - 2*r, z - 0.1]);
        cylinder(r = r, h = 0.1);
    }
}

// --- module : base du boîtier ---
module base_boitier() {
    difference() {
        // volume externe de la boîte avec coins arrondis
        cube_arrondi(longueur_externe, largeur_externe, hauteur_boite, rayon_boitier);
        
        // évidement interne principal
        translate([epaisseur_paroi, epaisseur_paroi, epaisseur_paroi])
            cube([longueur_interne + jeu, largeur_interne + jeu, hauteur_boite]);
        
        // ouverture frontale pour le port micro-usb / usb-c du wemos d1
        translate([-1, (largeur_externe / 2) - 6, epaisseur_paroi])
            cube([epaisseur_paroi + 2, 12, 5]);
        
        // fente à l'arrière pour faire passer la nappe plate du clavier
        translate([longueur_externe - epaisseur_paroi - 1, (largeur_externe / 2) - 7, hauteur_boite - 1.5])
            cube([epaisseur_paroi + 2, 14, 2]);
    }
    
    // bandes latérales de soutien (gauche et droite)
    translate([epaisseur_paroi, epaisseur_paroi, epaisseur_paroi])
        cube([longueur_interne + jeu, bande_largeur, bande_epaisseur]);
        
    translate([epaisseur_paroi, largeur_externe - epaisseur_paroi - bande_largeur, epaisseur_paroi])
        cube([longueur_interne + jeu, bande_largeur, bande_epaisseur]);

    // barre de stop transversale pour bloquer le wemos d1 à la longueur exacte
    // positionnée juste après la longueur du wemos pour l'empêcher de reculer
    translate([epaisseur_paroi + wemos_longueur + (jeu/2), epaisseur_paroi, epaisseur_paroi])
        cube([stop_largeur, largeur_interne + jeu, stop_hauteur]);
}

// --- module : couvercle avec emplacement clavier ---
module couvercle() {
    difference() {
        union() {
            // plaque supérieure externe du couvercle avec coins arrondis
            cube_arrondi(longueur_externe, largeur_externe, epaisseur_paroi, rayon_boitier);
            
            // lèvre interne pour emboîter le couvercle
            translate([epaisseur_paroi + (jeu/2), epaisseur_paroi + (jeu/2), -epaisseur_paroi])
                cube([longueur_interne, largeur_interne, epaisseur_paroi]);
        }
        
        // évidement interne de la lèvre
        translate([epaisseur_paroi * 2, epaisseur_paroi * 2, -epaisseur_paroi - 1])
            cube([longueur_interne - (epaisseur_paroi * 2), largeur_interne - (epaisseur_paroi * 2), epaisseur_paroi + 2]);
            
        // renfoncement sur le dessus pour intégrer le clavier (réactivé à 57x20mm)
        translate([(longueur_externe - clavier_longueur) / 2, (largeur_externe - clavier_largeur) / 2, epaisseur_paroi - clavier_epaisseur])
            cube([clavier_longueur, clavier_largeur, clavier_epaisseur + 1]);
            
        // fente débouchante pour glisser la nappe à l'intérieur
        translate([longueur_externe - epaisseur_paroi - 6, (largeur_externe - 12) / 2, -epaisseur_paroi - 2])
            cube([4, 12, hauteur_boite]);
    }
}
